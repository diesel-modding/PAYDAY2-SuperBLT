#include "lua_functions.h"
#include "subhook.h"

#include "tweaker/xmltweaker.h"

#include <thread>
#include <util/util.h>

static std::thread::id main_thread_id;
static subhook::Hook applicationUpdateDetour, newStateDetour, luaCloseDetour, node_from_xmlDetour;

static void init_idstring_pointers()
{
	char* tmp;

	tmp = (char*)try_open_property_match_resolver;
	tmp += 0x3A;
	tmp += *(unsigned int*)tmp + 4; // 64-Bit RIP Offset MOV

	blt::platform::last_loaded_name = (blt::idstring*)tmp;

	tmp = (char*)try_open_property_match_resolver;
	tmp += 0x33;
	tmp += *(unsigned int*)tmp + 4; // 64-Bit RIP Offset MOV

	blt::platform::last_loaded_ext = (blt::idstring*)tmp;
}

static void LuaInterface__newstate_new(void* _this, bool libs, bool main, char a)
{
	subhook::ScopedHookRemove scoped_remove(&newStateDetour);

	LuaInterface__newstate(_this, libs, main, a);

	lua_State* L = (lua_State*)*((void**)_this);
	if (L /* && libs && main*/)
	{
		printf("Lua State: %p\n", (void*)L);
		blt::lua_functions::initiate_lua(L);
	}
}

static void* application_update_new(void* _unk0, int* _unk1, int* _unk2)
{
	subhook::ScopedHookRemove scoped_remove(&applicationUpdateDetour);

	if (std::this_thread::get_id() == main_thread_id)
	{
		blt::lua_functions::update();
	}

	return application_update(_unk0, _unk1, _unk2);
}

static void lua_close_new(lua_State* L)
{
	subhook::ScopedHookRemove scoped_remove(&luaCloseDetour);

	blt::lua_functions::close(L);
	lua_close_exe(L);
}

static void node_from_xml_new(void* node, char* data, int* len)
{
	subhook::ScopedHookRemove scoped_remove(&node_from_xmlDetour);

	char* modded = raidhook::tweaker::tweak_raid_xml(data, *len);
	int modLen = *len;

	if (modded != data)
	{
		modLen = strlen(modded);
	}

	node_from_xml(node, modded, &modLen);

	raidhook::tweaker::free_tweaked_raid_xml(modded);
}

static void setup_platform_game()
{
	main_thread_id = std::this_thread::get_id();

	applicationUpdateDetour.Install(application_update, application_update_new,
	                                subhook::HookFlags::HookFlag64BitOffset);
	newStateDetour.Install(LuaInterface__newstate, LuaInterface__newstate_new, subhook::HookFlags::HookFlag64BitOffset);
	luaCloseDetour.Install(lua_close_exe, lua_close_new, subhook::HookFlags::HookFlag64BitOffset);
	node_from_xmlDetour.Install(node_from_xml, node_from_xml_new, subhook::HookFlags::HookFlag64BitOffset);

	init_idstring_pointers();
}