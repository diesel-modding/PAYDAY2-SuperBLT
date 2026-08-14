//
// Created by znix on 24/01/2021.
//

#include "LuaAssetDb.h"

#include <errno.h>
#include <fstream>
#include <inttypes.h>
#include <platform.h>
#include <string.h>
#include <util/util.h>

using blt::idstring;

static idstring to_idstring(lua_State* L, int idx, const char* err_name = nullptr)
{
	const char* str;
	if (err_name)
	{
		if (lua_type(L, idx) != LUA_TSTRING)
		{
			luaL_error(L, "Invalid type '%s' to SBLT DB function opt '%s' - needed string", lua_typename(L, idx),
			           err_name);
		}
		str = lua_tostring(L, idx);
	}
	else
	{
		str = luaL_checkstring(L, idx);
	}
	int len = strlen(str);

	// If the name is a 17 or 18-byte-long string starting with a hash, it's the plain hash
	if (str[0] == '#' && (len == 17 || (len == 18 && str[17] == '!')))
	{
		char* end_ptr;
		idstring value = strtoull(str + 1, &end_ptr, 16);
		if (*end_ptr && end_ptr - str != 17)
			luaL_error(L, "Failed to parse raw idstring '%s': parsing stopped at '%s'", str, end_ptr);
		// If string len is 17, we're done
		if (len == 17)
		{
			return value;
		}

		// Otherwise, the last character is an exclamation mark so swap endianness
		idstring endian_swapped = 0;
		for (unsigned i = 0; i < sizeof(idstring); i++)
		{
			endian_swapped |= ((value >> (i * 8)) & 0xFF) << ((sizeof(idstring) - 1 - i) * 8);
		}
		return endian_swapped;
	}

	return blt::idstring_hash(str);
}

// register a custom Wwise soundbank so that Diesel can load it
static int ldb_register_custom_soundbank(lua_State* L)
{
	const char* soundbankPath = lua_tostring(L, 1);

	if (soundbankPath)
	{
		blt::platform::wwise::RegisterCustomSoundbank(soundbankPath);
	}
	return 0;
}

// unregister a previously registered Wwise soundbank
static int ldb_unregister_custom_soundbank(lua_State* L)
{
	const char* soundbankPath = lua_tostring(L, 1);

	if (soundbankPath)
	{
		blt::platform::wwise::UnregisterCustomSoundbank(soundbankPath);
	}
	return 0;
}

static int ldb_register_custom_streamed_wem(lua_State* L)
{
	// String only, cannot trust Lua numbers with hashed values
	const char* wemId = lua_tostring(L, 1);
	const char* dbPath = lua_tostring(L, 2);

	if (wemId && dbPath)
	{
		blt::platform::wwise::RegisterCustomStreamedWemPath(std::stoul(wemId), dbPath);
	}

	return 0;
}

static int ldb_unregister_custom_streamed_wem(lua_State* L)
{
	// String only, cannot trust Lua numbers with hashed values
	const char* wemId = lua_tostring(L, 1);

	if (wemId)
	{
		blt::platform::wwise::UnregisterCustomStreamedWemPath(std::stoul(wemId));
	}

	return 0;
}

void load_lua_asset_db(lua_State* L)
{
	// (note: ldb = Lua asset DB)
	luaL_Reg vmLib[] = {
		{"register_custom_soundbank", ldb_register_custom_soundbank},
		{"unregister_custom_soundbank", ldb_unregister_custom_soundbank},
		{"register_custom_streamed_wem", ldb_register_custom_streamed_wem},
		{"unregister_custom_streamed_wem", ldb_unregister_custom_streamed_wem},

		{nullptr, nullptr},
	};

	lua_newtable(L);
	luaL_register(L, nullptr, vmLib);
	lua_setfield(L, -2, "asset_db");
}
