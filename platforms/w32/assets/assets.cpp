//
// Created by ZNix on 16/08/2020.
//

#include <vector>
#define INCLUDE_TRY_OPEN_FUNCTIONS

#include "assets.h"
#include "platform.h"
#include "subhook.h"

#include <stdio.h>

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
}
