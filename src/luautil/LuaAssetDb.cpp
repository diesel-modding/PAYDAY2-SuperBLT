//
// Created by znix on 24/01/2021.
//

#include "LuaAssetDb.h"

#include <dbutil/DB.h>
#include <errno.h>
#include <fstream>
#include <inttypes.h>
#include <platform.h>
#include <string.h>
#include <util/util.h>

using blt::idstring;
using blt::db::DieselBundle;
using blt::db::DieselDB;
using blt::db::DslFile;

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

static DslFile* find_file(lua_State* L)
{
	idstring name = to_idstring(L, 1);
	idstring ext = to_idstring(L, 2);

	// 3rd arg is an options table
	idstring lang = 0;
	if (lua_istable(L, 3))
	{
		lua_getfield(L, 3, "language");
		// Use toboolean instead of isnil to supplying false is the same as nil
		if (lua_toboolean(L, -1))
			lang = to_idstring(L, -1, "options.language");
		lua_pop(L, 1);
	}

	DslFile* file = DieselDB::Instance()->Find(name, ext);

	// If it's not found, stop here - otherwise we'll crash when finding the language ID
	if (!file)
		return nullptr;

	// Iterate though the different language versions of this file, until we find the one we need
	for (DslFile* fi = file; fi; fi = fi->next)
	{
		if (fi->langId == lang)
			return fi;
	}

	// Can't find anything with the correct language
	return nullptr;
}

static int ldb_load(lua_State* L)
{
	idstring name = to_idstring(L, 1);
	idstring ext = to_idstring(L, 2);

	bool optional = false; // Is it valid for the file to not exist?

	if (lua_istable(L, 3))
	{
		lua_getfield(L, 3, "optional");
		optional = lua_toboolean(L, -1);
		lua_pop(L, 1);
	}

	DslFile* file = find_file(L);

	if (!file) // Asset does not exist
	{
		if (optional)
		{
			lua_pushnil(L);
			return 1;
		}

		char msg[1024];
		snprintf(msg, sizeof(msg) - 1, "AssetDB: could not load asset " IDPFP " - not found in database", name, ext);
		luaL_error(L, msg);
		return 0; // Placate CLion's null warning thing, luaL_error never returns
	}

	errno = 0;
	try
	{
		std::ifstream fi;
		fi.exceptions(std::ios::failbit);
		fi.open(file->bundle->path, std::ios::binary);

		std::vector<uint8_t> data = file->ReadContents(fi);
		lua_pushlstring(L, (const char*)data.data(), data.size());
		return 1;
	}
	catch (const std::ios::failure& ex)
	{
		luaL_error(L, "Failed to read bundle: io error: %s", strerror(errno));
		return 0; // Will never happen, luaL_error does not return
	}
}

static int ldb_has(lua_State* L)
{
	DslFile* file = find_file(L);
	lua_pushboolean(L, file != nullptr);
	return 1;
}

#ifdef _WIN32
// register a custom Wwise soundbank so that Diesel can load it
static int ldb_register_custom_soundbank(lua_State* L)
{
	const char* soundbankPath = lua_tostring(L, 1);

	if (soundbankPath)
	{
		blt::platform::win32::wwise::RegisterCustomSoundbank(soundbankPath);
	}
	return 0;
}

// unregister a previously registered Wwise soundbank
static int ldb_unregister_custom_soundbank(lua_State* L)
{
	const char* soundbankPath = lua_tostring(L, 1);

	if (soundbankPath)
	{
		blt::platform::win32::wwise::UnregisterCustomSoundbank(soundbankPath);
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
		blt::platform::win32::wwise::RegisterCustomStreamedWemPath(std::stoul(wemId), dbPath);
	}

	return 0;
}

static int ldb_unregister_custom_streamed_wem(lua_State* L)
{
	// String only, cannot trust Lua numbers with hashed values
	const char* wemId = lua_tostring(L, 1);

	if (wemId)
	{
		blt::platform::win32::wwise::UnregisterCustomStreamedWemPath(std::stoul(wemId));
	}

	return 0;
}
#endif

void load_lua_asset_db(lua_State* L)
{
	// (note: ldb = Lua asset DB)
	luaL_Reg vmLib[] = {
		{"read_file", ldb_load},
		{"has_file", ldb_has},

#ifdef _WIN32
		{"register_custom_soundbank", ldb_register_custom_soundbank},
		{"unregister_custom_soundbank", ldb_unregister_custom_soundbank},
		{"register_custom_streamed_wem", ldb_register_custom_streamed_wem},
		{"unregister_custom_streamed_wem", ldb_unregister_custom_streamed_wem},
#endif

		{nullptr, nullptr},
	};

	lua_newtable(L);
	luaL_openlib(L, nullptr, vmLib, 0);
	lua_setfield(L, -2, "asset_db");
}
