#include "dbutil/Datastore.h"
#include "tweaker/db_hooks.h"

#include "convert.h"
#include "dbutil/Archive.h"
#include "platform.h"
#include "subhook.h"
#include "util/util.h"

#include <windows.h>

#include <synchapi.h>
#include <unordered_set>

using raidhook::tweaker::dbhook::hook_asset_load;

// The signature is the same for all try_open methods, so one typedef will work for all of them.
typedef void(__thiscall* try_open_t)(void* this_, void* archive, blt::idstring* type, blt::idstring* name,
                                     unsigned long long u1, unsigned long long u2);

static void hook_load(try_open_t orig, subhook::Hook& hook, void* this_, Archive* archive, blt::idstring* type,
                      blt::idstring* name, unsigned long long u1, unsigned long long u2);

#define DECLARE_PASSTHROUGH(func)                                                                                 \
	static subhook::Hook hook_##func;                                                                             \
	void stub_##func(void* this_, void* archive, blt::idstring* type, blt::idstring* name, unsigned long long u1, \
	                 unsigned long long u2)                                                                       \
	{                                                                                                             \
		hook_load((try_open_t)func, hook_##func, this_, (Archive*)archive, type, name, u1, u2);                   \
	}

#define DECLARE_PASSTHROUGH_ARRAY(id)                                                                              \
	static subhook::Hook hook_##id;                                                                                \
	void stub_##id(void* this_, void* archive, blt::idstring* type, blt::idstring* name, unsigned long long u1,    \
	               unsigned long long u2)                                                                          \
	{                                                                                                              \
		hook_load((try_open_t)try_open_functions.at(id), hook_##id, this_, (Archive*)archive, type, name, u1, u2); \
	}

static bool IsFileDataStore(void* datastore)
{
	void** vtable = *(void***)datastore;
	void* writeFn = vtable[1];
	return writeFn == dsl_FileDataStore_write;
}

static std::unordered_set<blt::idstring> GetScriptdataTypes()
{
	std::vector<std::string> names = {
		"continents", "continent",        "dialog", "dialog_index",  "environment",  "mission",       "nav_data",
		"cover_data", "sequence_manager", "world",  "world_cameras", "world_sounds", "world_setting", "objective",
		"hint",       "achievment",
	};
	std::unordered_set<blt::idstring> result;

	for (const std::string& name : names)
	{
		result.insert(blt::idstring_hash(name));
	}

	return result;
}

static const std::unordered_set<blt::idstring> SCRIPTDATA_TYPES = GetScriptdataTypes();

static void DeleteDatastore(BLTAbstractDataStore* datastore, int refcountId)
{
	// Do the same thing as an Archive would
	// Datastores use this big global reference count system. Objects have an ID, which you can then
	// use to increment and decrement their reference count.
	// If we're the last one to use this object - which we almost certainly are - then delete it.

	int datastoreRefCount = DecreaseRefCountById(refcountId);
	if (datastoreRefCount != 0)
		return;

	using DtorFn = void (*)(void* thisPtr, bool freeMemory);
	void* vtable = *(void***)datastore;
	DtorFn dtor = *(DtorFn*)vtable;
	dtor(datastore, true);
}

using ConversionFn = std::function<std::vector<uint8_t>(std::vector<uint8_t>&& origData, const std::string& name)>;

static void ConvertData(Archive* archive, const ConversionFn& conversionFn)
{
	// Load the base data
	size_t rawSize = archive->datastore->size();
	std::vector<uint8_t> rawData(rawSize);
	archive->datastore->read(0, rawData.data(), rawSize);

	// We don't need the old datastore any more
	DeleteDatastore(archive->datastore, archive->datastoreRefCountId);
	archive->datastore = nullptr;
	archive->datastoreRefCountId = -1; // Hopefully make any crashes relatively obvious

	// Covert the data
	std::string filePath = archive->name.ToCXX();
	std::vector<uint8_t> convertedData = conversionFn(std::move(rawData), filePath);

	archive->length = convertedData.size(); // Read this before std::move
	archive->datastore = new BLTStringDataStore(std::move(convertedData));
	archive->datastoreRefCountId = AllocateRefCountId();

	// It's very unlikely, but if this file was somehow marked as compressed, clear that.
	archive->maybeCompressedSize = 0;
}

static void hook_load(try_open_t orig, subhook::Hook& hook, void* this_, Archive* archive, blt::idstring* type,
                      blt::idstring* name, unsigned long long u1, unsigned long long u2)
{
	// Try hooking this asset, and see if we need to handle it differently
	BLTAbstractDataStore* datastore = nullptr;
	int64_t pos = 0, len = 0;
	std::string ds_name;
	bool found = hook_asset_load(blt::idfile(*name, *type), &datastore, &pos, &len, ds_name, false);

	// If we do need to load a custom asset as a result of that, do so now
	// That just means making our own version of of the archive
	if (found)
	{
		Archive::Constructor(archive, ds_name, datastore, pos, len, false);
		return;
	}

	subhook::ScopedHookRemove scoped_remove(&hook);
	orig(this_, archive, type, name, u1, u2);

	// Read the probably_not_loaded_flag to see if this archive failed to load - if so try again but also
	// look for hooks with the fallback bit set
	if (archive->probablyNotLoadedFlag)
	{
		if (hook_asset_load(blt::idfile(*name, *type), &datastore, &pos, &len, ds_name, true))
		{
			Archive::Constructor(archive, ds_name, datastore, pos, len, false);
			return;
		}
	}

	// At this point, we just need to perform conversion for 32-bit assets.
	//
	// The assets loaded from crates are either a ConstMemoryDataStore, or a FileDataStore pointing to some
	// non-zero offset within a crate file. It's unlikely we'll get a ConstMemoryDataStore here as the file
	// is 'upgraded' to that after being read, but check for it just in case.
	//
	// By comparison, a file loaded from DB:create_entry or mod_overrides will be a FileDataStore with
	// a position of zero.

	// No file?
	if (!archive->datastore)
		return;

	// Not a file store, or pointing somewhere inside a crate file?
	if (!IsFileDataStore(archive->datastore) || archive->position)
		return;

	// This is a file loaded from an external file. Depending on the type, inject our converter.

	if (SCRIPTDATA_TYPES.contains(*type))
	{
		ConvertData(archive, ConvertScriptData);

		// char msg[100];
		// snprintf(msg, sizeof(msg), "Loading %016llx.%016llx", *name, *type);
		// RAIDHOOK_LOG_LOG(msg);

		// Don't attempt any further format conversion.
		return;
	}

	if (*type == blt::idstring_hash("bnk"))
	{
		// The soundbanks can be quite big, so it's probably best not to load them into memory
		// immediately ourselves if they're already 64-bit.
		// (though the game will ultimately do this anyway, we're just saving a memcpy)
		if (CheckWwiseSoundbankRequiresConversion(archive->datastore))
		{
			ConvertData(archive, ConvertWwiseSoundbank);
		}

		// Don't attempt any further format conversion.
		return;
	}
}

static void setup_extra_asset_hooks()
{
}
