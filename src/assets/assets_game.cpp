#include "dbutil/Datastore.h"
#include "tweaker/db_hooks.h"

#include "platform.h"
#include "subhook.h"

#define WIN32_LEAN_AND_MEAN
#include <synchapi.h>
#include <windows.h>

using raidhook::tweaker::dbhook::hook_asset_load;

// A wrapper class to store strings in the format that PAYDAY 2 does
// Since Microsoft might (and it seems they have) change their string class, we
// need this.
class PDString
{
  public:
	explicit PDString(std::string str) : s(std::move(str))
	{
		data = s.c_str();
		len = s.length();
		cap = s.length();
	}

  private:
	// The data layout that mirrors RAID's string, must be 24 bytes
	const char* data;
	uint8_t padding[12]{};
	int len;
	int cap;

	// String that can deal with the actual data ownership
	std::string s;
};

static_assert(sizeof(PDString) == 32 + sizeof(std::string), "PDString is the wrong size!");

// The signature is the same for all try_open methods, so one typedef will work for all of them.
typedef void(__thiscall* try_open_t)(void* this_, void* archive, blt::idstring* type, blt::idstring* name,
                                     unsigned long long u1, unsigned long long u2);

static void hook_load(try_open_t orig, subhook::Hook& hook, void* this_, void* archive, blt::idstring* type,
                      blt::idstring* name, unsigned long long u1, unsigned long long u2);

#define DECLARE_PASSTHROUGH(func)                                                                                 \
	static subhook::Hook hook_##func;                                                                             \
	void stub_##func(void* this_, void* archive, blt::idstring* type, blt::idstring* name, unsigned long long u1, \
	                 unsigned long long u2)                                                                       \
	{                                                                                                             \
		hook_load((try_open_t)func, hook_##func, this_, archive, type, name, u1, u2);                             \
	}

#define DECLARE_PASSTHROUGH_ARRAY(id)                                                                           \
	static subhook::Hook hook_##id;                                                                             \
	void stub_##id(void* this_, void* archive, blt::idstring* type, blt::idstring* name, unsigned long long u1, \
	               unsigned long long u2)                                                                       \
	{                                                                                                           \
		hook_load((try_open_t)try_open_functions.at(id), hook_##id, this_, archive, type, name, u1, u2);        \
	}

// Approximate structure from IDA, zero idea how accurate it is, but we've got the important ones here.
struct Archive
{
	char char0;
	uint8_t gap1[15];
	uint64_t qword10;
	uint64_t qword18;

	uint64_t position;
	uint64_t length;

	uint64_t qword30;
	uint16_t word38;
	uint8_t gap3A[6];
	uint64_t qword40;

	struct _RTL_CRITICAL_SECTION rtl_critical_section48;
	BLTAbstractDataStore* datastore;

	uint32_t dword78;
};

// This got inlined in the latest build, so we have to recreate it ourselves.
static void* Archive_ctor(Archive* archive, PDString* pd_name, BLTAbstractDataStore* datastore, int64_t pos,
                          int64_t len, bool ukn)
{
	archive->position = pos;
	archive->length = len;
	archive->qword30 = 0i64;
	archive->word38 = ukn;
	archive->qword40 = 0i64;

#pragma warning(suppress : 6031) // Complains about not using the return data.
	InitializeCriticalSectionAndSpinCount(&archive->rtl_critical_section48, 0xFA0u);

	archive->datastore = datastore;
	archive->dword78 = -1;
	if (datastore)
		archive->dword78 = Archive_ctor_datastore_thing();

	return archive;
}

static void hook_load(try_open_t orig, subhook::Hook& hook, void* this_, void* archive, blt::idstring* type,
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
		PDString pd_name(ds_name);
		Archive_ctor((Archive*)archive, &pd_name, datastore, pos, len, false);
		return;
	}

	if (*name == 185182019423013513u)
	{
		printf("camera stuff");
	}

	subhook::ScopedHookRemove scoped_remove(&hook);
	orig(this_, archive, type, name, u1, u2);

	// Read the probably_not_loaded_flag to see if this archive failed to load - if so try again but also
	// look for hooks with the fallback bit set
	bool probably_not_loaded_flag = *(bool*)((char*)archive + 0x38);
	if (probably_not_loaded_flag)
	{
		if (hook_asset_load(blt::idfile(*name, *type), &datastore, &pos, &len, ds_name, true))
		{
			PDString pd_name(ds_name);
			Archive_ctor((Archive*)archive, &pd_name, datastore, pos, len, false);
			return;
		}
	}
}

static void setup_extra_asset_hooks()
{
}

#define HOOK_OPTION subhook::HookOptions::HookOption64BitOffset
