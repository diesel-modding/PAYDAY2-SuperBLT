//
// Created by ZNix on 16/08/2020.
//

#include <vector>
#define INCLUDE_TRY_OPEN_FUNCTIONS

#include "assets.h"
#include "platform.h"
#include "subhook.h"

#include "util/util.h"

#include <stdio.h>

#include <map>
#include <mutex>
#include <string>
#include <tweaker/db_hooks.h>
#include <utility>

using pd2hook::tweaker::dbhook::hook_asset_load;

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
	// The data layout that mirrors PD2's string, must be 24 bytes
	const char* data;
	uint8_t padding[12]{};
	int len;
	int cap;

	// String that can deal with the actual data ownership
	std::string s;
};
static_assert(sizeof(PDString) == 24 + sizeof(std::string), "PDString is the wrong size!");

// The signature is the same for all try_open methods, so one typedef will work for all of them.
typedef void(__thiscall* try_open_t)(void* this_, void* archive, blt::idstring type, blt::idstring name, int a, int b);

static void hook_load(try_open_t orig, subhook::Hook& hook, void* this_, void* archive, blt::idstring type,
                      blt::idstring name, int u1, int u2);

#define DECLARE_PASSTHROUGH_ARRAY(id)                                                                              \
	static subhook::Hook hook_##id;                                                                                \
	void __fastcall stub_##id(void* this_, int edx, void* archive, blt::idstring type, blt::idstring name, int u1, \
	                          int u2)                                                                              \
	{                                                                                                              \
		hook_load((try_open_t)try_open_functions.at(id), hook_##id, this_, archive, type, name, u1, u2);           \
	}

// Four hooks for the other try_open functions: property_match_resolver, language_resolver, english_resolver and
// funcptr_resolver
DECLARE_PASSTHROUGH_ARRAY(0)
DECLARE_PASSTHROUGH_ARRAY(1)
DECLARE_PASSTHROUGH_ARRAY(2)
DECLARE_PASSTHROUGH_ARRAY(3)

static void hook_load(try_open_t orig, subhook::Hook& hook, void* this_, void* archive, blt::idstring type,
                      blt::idstring name, int u1, int u2)
{
	// Try hooking this asset, and see if we need to handle it differently
	BLTAbstractDataStore* datastore = nullptr;
	int64_t pos = 0, len = 0;
	std::string ds_name;
	bool found = hook_asset_load(blt::idfile(name, type), &datastore, &pos, &len, ds_name, false);

	// If we do need to load a custom asset as a result of that, do so now
	// That just means making our own version of of the archive
	if (found)
	{
		PDString pd_name(ds_name);
		Archive_ctor(archive, &pd_name, datastore, pos, len, false, 0);
		return;
	}

	subhook::ScopedHookRemove scoped_remove(&hook);
	orig(this_, archive, type, name, u1, u2);

	// Read the probably_not_loaded_flag to see if this archive failed to load - if so try again but also
	// look for hooks with the fallback bit set
	bool probably_not_loaded_flag = *(bool*)((char*)archive + 0x30);
	if (probably_not_loaded_flag)
	{
		if (hook_asset_load(blt::idfile(name, type), &datastore, &pos, &len, ds_name, true))
		{
			// Note the deadbeef is for the stack padding argument - see the comment on this signature's declaration
			// for more information.
			PDString pd_name(ds_name);
			Archive_ctor(archive, &pd_name, datastore, pos, len, false, 0);
			return;
		}
	}
}

static subhook::Hook WwDevice_loadBankIdstringDetour;
static subhook::Hook WwDevice_idToEntryDetour;

class SoundBank;

unsigned int GetWwiseHash(const char* str)
{
	unsigned int hash = 2166136261;
	for (int i = 0; i < strlen(str); i++)
	{
		hash = str[i] ^ (16777619 * hash);
	}
	return hash;
}

// Access happens on two different threads
std::mutex customWwiseMapsMutex;
std::map<blt::idstring, std::string> customWwiseSoundbankNames;
std::map<unsigned int, blt::idstring> customWwiseIdToEntryNames;

class sound_WwDevice
{
  public:
	virtual ~sound_WwDevice() = 0;
	virtual void unneeded_virtual_1() = 0;
	virtual void unneeded_virtual_2() = 0;
	virtual void unneeded_virtual_3() = 0;
	virtual void unneeded_virtual_4() = 0;
	virtual void unneeded_virtual_5() = 0;
	virtual SoundBank* load_bank_idstring(idstr bank, int async) = 0;
	virtual SoundBank* load_bank_string(const char* bank, bool async) = 0;

  public:
	SoundBank* load_bank_idstring_new(idstr bank, int async)
	{
		subhook::ScopedHookRemove scoped_remove(&WwDevice_loadBankIdstringDetour);

		SoundBank* soundbank = (SoundBank*)sound_WwDevice_load_bank_idstring((void*)this, bank, async);

		if (soundbank == nullptr) {
			std::lock_guard customListLock(customWwiseMapsMutex);
			if (customWwiseSoundbankNames.find(bank._id) != customWwiseSoundbankNames.end())
			{
				soundbank = load_bank_string(customWwiseSoundbankNames[bank._id].c_str(), async);
			}
		}

		return soundbank;
	}

	idstr* id_to_entry_new(idstr* result, unsigned int wwise_id)
	{
		subhook::ScopedHookRemove scoped_remove(&WwDevice_idToEntryDetour);

		result = sound_WwDevice_id_to_entry((void*)this, result, wwise_id);

		if (result->_id != 0x8DB63936938575BF) // empty idstring (""), default return value from sound::WwDevice::id_to_entry if no match was found
		{
			return result;
		}

		std::lock_guard customListLock(customWwiseMapsMutex);

		if (customWwiseIdToEntryNames.find(wwise_id) != customWwiseIdToEntryNames.end())
		{
			result->_id = customWwiseIdToEntryNames[wwise_id];
		}

		return result;
	}
};

void blt::win32::InitAssets()
{
#define SETUP_PASSTHROUGH_ARRAY(id) hook_##id.Install(try_open_functions.at(id), stub_##id)
	if (!try_open_functions.empty())
		SETUP_PASSTHROUGH_ARRAY(0);
	if (try_open_functions.size() > 1)
		SETUP_PASSTHROUGH_ARRAY(1);
	if (try_open_functions.size() > 2)
		SETUP_PASSTHROUGH_ARRAY(2);
	if (try_open_functions.size() > 3)
		SETUP_PASSTHROUGH_ARRAY(3);

	WwDevice_loadBankIdstringDetour.Install(sound_WwDevice_load_bank_idstring, GetAddressOfClassFunction(&sound_WwDevice::load_bank_idstring_new));
	WwDevice_idToEntryDetour.Install(sound_WwDevice_id_to_entry, GetAddressOfClassFunction(&sound_WwDevice::id_to_entry_new));
}

void blt::platform::win32::wwise::RegisterCustomSoundbank(const char* dbPath)
{
	if (!dbPath)
		return;

	std::lock_guard customListLock(customWwiseMapsMutex);

	blt::idstring hashedPath = blt::idstring_hash(dbPath);
	unsigned int wwiseHash = GetWwiseHash(dbPath);
	customWwiseSoundbankNames.insert(std::make_pair(hashedPath, dbPath));
	customWwiseIdToEntryNames.insert(std::make_pair(wwiseHash, hashedPath));
}

void blt::platform::win32::wwise::UnregisterCustomSoundbank(const char* dbPath)
{
	if (!dbPath)
		return;

	std::lock_guard customListLock(customWwiseMapsMutex);

	blt::idstring hashedPath = blt::idstring_hash(dbPath);
	unsigned int wwiseHash = GetWwiseHash(dbPath);
	if (customWwiseSoundbankNames.find(hashedPath) != customWwiseSoundbankNames.end())
		customWwiseSoundbankNames.erase(hashedPath);
	if (customWwiseIdToEntryNames.find(wwiseHash) != customWwiseIdToEntryNames.end())
		customWwiseIdToEntryNames.erase(wwiseHash);
}

void blt::platform::win32::wwise::RegisterCustomStreamedWemPath(unsigned int wemId, const char* dbPath)
{
	if (!dbPath)
		return;

	std::lock_guard customListLock(customWwiseMapsMutex);

	customWwiseIdToEntryNames.insert(std::make_pair(wemId, blt::idstring_hash(dbPath)));
}

void blt::platform::win32::wwise::UnregisterCustomStreamedWemPath(unsigned int wemId)
{
	std::lock_guard customListLock(customWwiseMapsMutex);

	if (customWwiseIdToEntryNames.find(wemId) != customWwiseIdToEntryNames.end())
		customWwiseIdToEntryNames.erase(wemId);
}
