#include "InitState.h"
#include <mxml.h>

#include "lua.h"

#include "platform.h"
#include "lua_functions.h"

#include "util/util.h"
#include "threading/queue.h"
#include "http/http.h"
#include "debug/blt_debug.h"
#include "tweaker/xmltweaker.h"
#include "tweaker/wren_lua_interface.h"
#include "plugins/plugins.h"
#include "scriptdata/ScriptData.h"
#include "luautil/luautil.h"
#include "luautil/LuaAssetDb.h"
#include "luautil/LuaAsyncIO.h"
#include "dbutil/DB.h"

#ifdef ENABLE_XAUDIO
#include "xaudio/XAudio.h"
#endif

#include <format>
#include <thread>
#include <list>
#include <fstream>

namespace raidhook
{

	std::list<lua_State*> activeStates;
	static void add_active_state(lua_State* L)
	{
		activeStates.push_back(L);
	}

	static void remove_active_state(lua_State* L)
	{
		activeStates.remove(L);
	}

	bool check_active_state(void* L)
	{
		std::list<lua_State*>::iterator it;
		for (it = activeStates.begin(); it != activeStates.end(); it++)
		{
			if (*it == L)
			{
				return true;
			}
		}
		return false;
	}

	// Not tracking the error count here so it can automatically be reset to 0 whenever the Lua state is deleted and re-created (e.g.
	// when transitioning to / from the menu to a level)
	static void NotifyErrorOverlay(lua_State* L, const char* message)
	{
		lua_getglobal(L, "NotifyErrorOverlay");
		if (lua_isfunction(L, -1))
		{
			int args = 0;
			if (message)
			{
				lua_pushstring(L, message);
				args = 1;
			}
			int error = lua_pcall(L, args, 0, 0);
			if (error == LUA_ERRRUN)
			{
				// Don't bother logging the error since the error overlay is designed to be an optional component, just pop the error
				// message off the stack to keep it balanced
				lua_pop(L, 1);
				return;
			}
		}
		else
		{
			lua_pop(L, 1);
			static bool printed = false;
			if (!printed)
			{
				printf("Warning: Failed to find the NotifyErrorOverlay function in the Lua environment; no in-game notifications will be displayed for caught errors\n");
				printed = true;
			}
		}
	}

	// TODO deduplicate with that in LuaAsyncIO
	static void handled_pcall(lua_State* L, int nargs, int nresults)
	{
		int err = lua_pcall(L, nargs, nresults, 0);
		if (err == LUA_ERRRUN)
		{
			size_t len;
			RAIDHOOK_LOG_LOG(lua_tolstring(L, -1, &len));
		}
	}

	static int luaF_ispcallforced(lua_State* L)
	{
		lua_pushboolean(L, blt::platform::lua::GetForcePCalls());
		return 1;
	}

	static int luaF_forcepcalls(lua_State* L)
	{
		int args = lua_gettop(L);	// Number of arguments
		if (args < 1)
		{
			RAIDHOOK_LOG_WARN("blt.forcepcalls(): Called with no arguments, ignoring request");
			return 0;
		}

		blt::platform::lua::SetForcePCalls(lua_toboolean(L, 1));
		return 0;
	}

	static int luaH_getcontents(lua_State* L, bool files)
	{
		size_t len;
		const char* dirc = lua_tolstring(L, 1, &len);
		std::string dir(dirc, len);
		std::vector<std::string> directories;

		try
		{
			directories = Util::GetDirectoryContents(dir, files);
		}
		catch (int unused)
		{
			// Okay, okay - now shut up, compiler
			(void)unused;
			lua_pushboolean(L, false);
			return 1;
		}

		lua_createtable(L, 0, 0);

		std::vector<std::string>::iterator it;
		int index = 1;
		for (it = directories.begin(); it < directories.end(); it++)
		{
			if (*it == "." || *it == "..") continue;
			lua_pushinteger(L, index);
			lua_pushlstring(L, it->c_str(), it->length());
			lua_settable(L, -3);
			index++;
		}

		return 1;
	}

	static int luaF_getdir(lua_State* L)
	{
		return luaH_getcontents(L, true);
	}

	static int luaF_getfiles(lua_State* L)
	{
		return luaH_getcontents(L, false);
	}

	static int luaF_directoryExists(lua_State* L)
	{
		size_t len;
		const char* dirc = lua_tolstring(L, 1, &len);
		bool doesExist = Util::DirectoryExists(dirc);
		lua_pushboolean(L, doesExist);
		return 1;
	}

	static int luaF_fileExists(lua_State* L)
	{
		size_t len;
		const char* dirc = lua_tolstring(L, 1, &len);
		bool doesExist = Util::GetFileType(dirc) == Util::FileType_File;
		lua_pushboolean(L, doesExist);
		return 1;
	}

	static int luaF_unzipfile(lua_State* L)
	{
		size_t len;
		const char* archivePath = lua_tolstring(L, 1, &len);
		const char* extractPath = lua_tolstring(L, 2, &len);

		bool retVal = raidhook::ExtractZIPArchive(archivePath, extractPath);
		lua_pushboolean(L, retVal);
		return 1;
	}

	static int luaF_removeDirectory(lua_State* L)
	{
		size_t len;
		const char* directory = lua_tolstring(L, 1, &len);
		bool success = Util::RemoveEmptyDirectory(directory);
		lua_pushboolean(L, success);
		return 1;
	}

	static int luaF_pcall(lua_State* L)
	{
		int args = lua_gettop(L) - 1;

		lua_getglobal(L, "debug");
		lua_getfield(L, -1, "traceback");
		lua_remove(L, -2);
		// Do not index from the top (i.e. don't use a negative index)
		int errorhandler = lua_gettop(L) - args - 1;
		lua_insert(L, errorhandler);

		int result = lua_pcall(L, args, LUA_MULTRET, errorhandler);
		// lua_pcall() automatically pops the callee function and its arguments off the stack. Then, if no errors were encountered
		// during execution, it pushes the return values onto the stack, if any. Otherwise, if an error was encountered, it pushes
		// the error message onto the stack, which should manually be popped off when done using to keep the stack balanced
		if (result == LUA_ERRRUN)
		{
			size_t len;
			RAIDHOOK_LOG_ERROR(lua_tolstring(L, -1, &len));
			// This call pops the error message off the stack
			lua_pop(L, 1);
			return 0;
		}
		// Do not use lua_pop() as the callee function's return values may be present, which would pop one of those instead and leave
		// the error handler on the stack
		lua_remove(L, errorhandler);
		lua_pushboolean(L, result == 0);
		lua_insert(L, 1);

		//if (result != 0) return 1;

		return lua_gettop(L);
	}

	static int luaF_pcall_proper(lua_State* L)
	{
		luaL_checkany(L, 1);
		int status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
		lua_pushboolean(L, (status == 0));
		lua_insert(L, 1);
		return lua_gettop(L); // return status + all results
	}

	static int luaF_xpcall(lua_State* L)
	{
		// Args: func, err, ...

		// Move err from the 2nd index to the 1st index, so we have a continuous range for the function call
		lua_pushvalue(L, 2); // Copy err to the top
		lua_remove(L, 2); // Remove err
		lua_insert(L, 1); // Put error function under function to be called

		int status = lua_pcall(L, lua_gettop(L) - 2, LUA_MULTRET, 1);

		// Replace the error function (1st position) with the result
		lua_pushboolean(L, (status == 0));
		lua_replace(L, 1);

		return lua_gettop(L);  // return entire stack - status, possible err + all results
	}

	static int luaF_dofile(lua_State* L)
	{
		size_t length = 0;
		const char* filename = lua_tolstring(L, 1, &length);
		int error = luaL_loadfilex(L, filename, nullptr);
		if (error != 0)
		{
			size_t len;
			//		Logging::Log(filename, Logging::LOGGING_ERROR);
			RAIDHOOK_LOG_ERROR(lua_tolstring(L, -1, &len));
		}
		else
		{
			lua_getglobal(L, "debug");
			lua_getfield(L, -1, "traceback");
			lua_remove(L, -2);
			// Example stack for visualization purposes:
			// 3 debug.traceback
			// 2 compiled code as a self-contained function
			// 1 filename
			// Do not index from the top (i.e. don't use a negative index)
			int errorhandler = 2;
			lua_insert(L, errorhandler);

			error = lua_pcall(L, 0, 0, errorhandler);
			if (error == LUA_ERRRUN)
			{
				size_t len;
				//			Logging::Log(filename, Logging::LOGGING_ERROR);
				RAIDHOOK_LOG_ERROR(lua_tolstring(L, -1, &len));
				// This call pops the error message off the stack
				lua_pop(L, 1);
			}
			// Do not use lua_pop() as the callee function's return values may be present, which would pop one of those instead and
			// leave the error handler on the stack
			lua_remove(L, errorhandler);
		}
		return 0;
	}

	struct lua_http_data
	{
		int funcRef;
		int progressRef;
		int requestIdentifier;
		lua_State* L;
	};

	static void return_lua_http(HTTPItem* httpItem)
	{
		lua_http_data* ourData = (lua_http_data*)httpItem->data;
		if (!check_active_state(ourData->L))
		{
			delete ourData;
			return;
		}
		int statusCode = httpItem->httpStatusCode;
		bool querySucceeded = httpItem->errorCode == 0 && statusCode >= 100 && statusCode < 400;

		lua_rawgeti(ourData->L, LUA_REGISTRYINDEX, ourData->funcRef);
		lua_pushlstring(ourData->L, httpItem->httpContents.c_str(), httpItem->httpContents.length());
		lua_pushinteger(ourData->L, ourData->requestIdentifier);
		lua_newtable(ourData->L);
		lua_pushstring(ourData->L, "statusCode");
		lua_pushinteger(ourData->L, statusCode);
		lua_settable(ourData->L, -3);
		lua_pushstring(ourData->L, "querySucceeded");
		lua_pushboolean(ourData->L, querySucceeded);
		lua_settable(ourData->L, -3);
		lua_pushstring(ourData->L, "url");
		lua_pushstring(ourData->L, httpItem->url.c_str());
		lua_settable(ourData->L, -3);
		lua_pushstring(ourData->L, "headers");
		lua_newtable(ourData->L);
		for(std::pair<std::string, std::string> element:httpItem->responseHeaders)
		{
			lua_pushstring(ourData->L, element.first.c_str());
			lua_pushstring(ourData->L, element.second.c_str());
			lua_settable(ourData->L, -3);
		}
		lua_settable(ourData->L, -3);
		handled_pcall(ourData->L, 3, 0);
		luaL_unref(ourData->L, LUA_REGISTRYINDEX, ourData->funcRef);
		luaL_unref(ourData->L, LUA_REGISTRYINDEX, ourData->progressRef);
		delete ourData;
	}

	static void progress_lua_http(void* data, long progress, long total)
	{
		lua_http_data* ourData = (lua_http_data*)data;

		if (!check_active_state(ourData->L))
		{
			return;
		}

		if (ourData->progressRef == 0) return;
		lua_rawgeti(ourData->L, LUA_REGISTRYINDEX, ourData->progressRef);
		lua_pushinteger(ourData->L, ourData->requestIdentifier);
		lua_pushinteger(ourData->L, progress);
		lua_pushinteger(ourData->L, total);
		lua_pcall(ourData->L, 3, 0, 0);
	}

	static void call_hash_result(lua_State* L, int ref, std::string filename, std::string result)
	{
		if (!check_active_state(L))
		{
			return;
		}

		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		lua_pushlstring(L, result.c_str(), result.size());
		lua_pushlstring(L, filename.c_str(), filename.size());
		handled_pcall(L, 2, 0);

		luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}

	static int luaF_directoryhash(lua_State* L)
	{
		size_t length = 0;
		const char* filename = lua_tolstring(L, 1, &length);

		Util::FileType type = Util::GetFileType(filename);
		if (Util::GetFileType(filename) != Util::FileType_Directory)
		{
			luaL_error(L, "Invalid directory %s: type=%d (none=%d,file=%d,dir=%d)", filename, type,
			           Util::FileType_None, Util::FileType_File, Util::FileType_Directory);
		}

		if (!lua_isnoneornil(L, 2))
		{
			lua_pushvalue(L, 2);
			int callback = luaL_ref(L, LUA_REGISTRYINDEX);
			Util::RunAsyncHash(L, callback, filename, Util::GetDirectoryHash, call_hash_result);
			lua_pushboolean(L, true);
			return 1;
		}

		std::string hash = Util::GetDirectoryHash(filename);
		lua_pushlstring(L, hash.c_str(), hash.length());

		return 1;
	}

	static int luaF_filehash(lua_State* L)
	{
		size_t l = 0;
		const char * filename = lua_tolstring(L, 1, &l);

		Util::FileType type = Util::GetFileType(filename);
		if (Util::GetFileType(filename) != Util::FileType_File)
		{
			luaL_error(L, "Invalid file %s: type=%d (file=%d,dir=%d,none=%d)", filename, type,
			           Util::FileType_File, Util::FileType_Directory, Util::FileType_None);
		}

		if (!lua_isnoneornil(L, 2))
		{
			lua_pushvalue(L, 2);
			int callback = luaL_ref(L, LUA_REGISTRYINDEX);
			Util::RunAsyncHash(L, callback, filename, Util::GetFileHash, call_hash_result);
			lua_pushboolean(L, true);
			return 1;
		}

		std::string hash = Util::GetFileHash(filename);
		lua_pushlstring(L, hash.c_str(), hash.length());
		return 1;
	}

	static int HTTPReqIdent = 0;

	static int luaF_dohttpreq(lua_State* L)
	{
		int args = lua_gettop(L);
		int progressReference = 0;
		if (args >= 3)
		{
			progressReference = luaL_ref(L, LUA_REGISTRYINDEX);
		}

		int functionReference = luaL_ref(L, LUA_REGISTRYINDEX);
		size_t len;
		const char* url_c = lua_tolstring(L, 1, &len);
		std::string url = std::string(url_c, len);

		RAIDHOOK_LOG_LOG("HTTP request to " << std::string(url_c, len));

		lua_http_data* ourData = new lua_http_data();
		ourData->funcRef = functionReference;
		ourData->progressRef = progressReference;
		ourData->L = L;

		HTTPReqIdent++;
		ourData->requestIdentifier = HTTPReqIdent;

		std::unique_ptr<HTTPItem> reqItem(new HTTPItem());
		reqItem->call = return_lua_http;
		reqItem->data = ourData;
		reqItem->url = url;

		if (progressReference != 0)
		{
			reqItem->progress = progress_lua_http;
		}

		HTTPManager::GetSingleton()->LaunchHTTPRequest(std::move(reqItem));
		lua_pushinteger(L, HTTPReqIdent);
		return 1;
	}

	/*static int luaF_createconsole(lua_State* L) // TODO reenable
	{
		if (gbl_mConsole) return 0;
		gbl_mConsole = new CConsole();
		return 0;
	}

	static int luaF_destroyconsole(lua_State* L)
	{
		if (!gbl_mConsole) return 0;
		delete gbl_mConsole;
		gbl_mConsole = NULL;
		return 0;
	}*/

	static int luaF_print(lua_State* L)
	{
		int top = lua_gettop(L);
		std::stringstream stream;
		for (int i = 0; i < top; ++i)
		{
			stream << (i > 0 ? "    " : "");
			if (lua_isboolean(L, i + 1))
			{
				stream << (lua_toboolean(L, i + 1) ? "true" : "false");
			}
			else
			{
				size_t len;
				const char* str = lua_tolstring(L, i + 1, &len);
				stream << (str ? str : lua_typename(L, lua_type(L, i + 1)));
			}
		}
		RAIDHOOK_LOG_LUA(stream.str());

		return 0;
	}

	static int luaF_moveDirectory(lua_State* L)
	{
		size_t lf = 0;
		const char * fromStr = lua_tolstring(L, 1, &lf);
		size_t ld = 0;
		const char * destStr = lua_tolstring(L, 2, &ld);

		bool success = Util::MoveDirectory(fromStr, destStr);
		lua_pushboolean(L, success);
		return 1;
	}

	static int luaF_createDirectory(lua_State* L)
	{
		const char *path = lua_tostring(L, 1);
		bool success = Util::CreateDirectorySingle(path);
		lua_pushboolean(L, success);
		return 1;
	}

	static void build_xml_tree(lua_State* L, mxml_node_t* node)
	{
		// Create the main table
		lua_newtable(L);

		// Set the element name
		lua_pushstring(L, mxmlGetElement(node));
		lua_setfield(L, -2, "name");

		// Create the parameters table
		lua_newtable(L);
		for (int i = 0; i < mxmlElementGetAttrCount(node); i++)
		{
			const char *name;
			const char *value = mxmlElementGetAttrByIndex(node, i, &name);

			lua_pushstring(L, value);
			lua_setfield(L, -2, name);
		}

		lua_setfield(L, -2, "params");

		// Add all the child nodes
		mxml_node_t *child = mxmlGetFirstChild(node);
		int i = 1;
		while (child != NULL)
		{
			if (strncmp(mxmlGetElement(child), "!--", 3) != 0)
			{
				build_xml_tree(L, child);
				lua_rawseti(L, -2, i++);
			}

			child = mxmlGetNextSibling(child);
		}
	}

	static const char* mxml_last_error = NULL;
	static void handle_mxml_error(const char* error)
	{
		mxml_last_error = error;
	}

	static int luaF_parsexml(lua_State* L)
	{
		const char *xml = lua_tostring(L, 1);

		mxmlSetErrorCallback(handle_mxml_error);

		mxml_node_t *tree = mxmlLoadString(NULL, xml, MXML_IGNORE_CALLBACK);

		if (mxml_last_error)
		{
			RAIDHOOK_LOG_ERROR("Could not parse XML: Error and original file below");
			RAIDHOOK_LOG_ERROR(mxml_last_error);
			RAIDHOOK_LOG_ERROR(xml);

			mxml_last_error = NULL;

			lua_pushnil(L);

			return 1;
		}

		mxml_node_t *base = tree;
		if (base && !strncmp(mxmlGetElement(base), "?xml", 4))
		{
			base = mxmlGetFirstChild(base);
		}

		if (base)
		{
			build_xml_tree(L, base);
		}
		else
		{
			RAIDHOOK_LOG_ERROR("Parsed XML does not contain any nodes");
			RAIDHOOK_LOG_ERROR(xml);

			lua_pushnil(L);
		}

		mxmlDelete(tree);

		return 1;
	}

	// Basically the same thing as lua_topointer
	static int luaF_structid(lua_State* L)
	{
		if (lua_gettop(L) != 1)
		{
			luaL_error(L, "Signature: structid(struct)");
		}

		void *value_ptr = NULL;

		typedef struct
		{
			uint32_t gcptr32;     /* Pseudo 32 bit pointer. */
			uint32_t it;      /* Internal object tag. Must overlap MSW of number. */
		} TValue;

		const TValue **value;
		{
			char *ptr = (char*)L;
			ptr += 16; // Lua stack starts here, ends at +20
			value = (const TValue**)ptr;
		}

		if (lua_type(L, 1) == LUA_TUSERDATA || lua_islightuserdata(L, 1))
		{
			value_ptr = lua_touserdata(L, 1);
		}
		else if ((*value)->it > (~13u)) // Boiled down from lua_topointer in LuaJIT
		{
			value_ptr = (void*) (uint64_t) (*value)->gcptr32;
		}
		else
		{
			luaL_error(L, "Illegal argument - should be tvgcv (table) or userdata");
		}

		char buffer[9]; // 8 chars for the address, one for the null
		snprintf(buffer, sizeof(buffer), "%p", value_ptr);

		lua_pushstring(L, buffer);

		return 1;
	}

	static int luaF_ignoretweak(lua_State* L)
	{
		blt::idfile file;

		file.name = luaX_toidstring(L, 1);
		file.ext = luaX_toidstring(L, 2);

		tweaker::ignore_file(file);

		return 0;
	}

	static int luaF_load_native(lua_State* L)
	{
		std::string file(lua_tostring(L, 1));

		try
		{
			blt::plugins::Plugin *plugin = NULL;
			blt::plugins::PluginLoadResult result = blt::plugins::LoadPlugin(file, &plugin);

			// TODO some kind of UUID system to prevent issues with multiple mods having the same DLL

			int count = plugin->PushLuaValue(L);

			if (result == blt::plugins::plr_AlreadyLoaded)
			{
				lua_pushstring(L, "Already loaded");
			}
			else
			{
				lua_pushboolean(L, true);
			}

			lua_insert(L, -1 - count);
			return count + 1;

		}
		catch (std::string err)
		{
			luaL_error(L, err.c_str());
			return 0; // Fix the no-return compiler warning, but this will never be called
		}
	}

	static int luaF_blt_info(lua_State* L)
	{
		lua_newtable(L);

		blt::platform::GetPlatformInformation(L);

		return 1;
	}

	static int luaF_blt_version(lua_State* L)
	{
		HMODULE hModule;
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)luaF_blt_version, &hModule);
		char path[MAX_PATH + 1];
		size_t pathSize = GetModuleFileName(hModule, path, sizeof(path) - 1);
		path[pathSize] = '\0';

		DWORD verHandle = 0;
		UINT size = 0;
		LPBYTE lpBuffer = NULL;
		uint32_t verSize = GetFileVersionInfoSize(path, &verHandle);

		if (verSize == 0)
		{
			RAIDHOOK_LOG_ERROR(std::format("Error occurred while calling 'GetFileVersionInfoSize': {}", GetLastError()));
			lua_pushstring(L, "0.0.0.0");
			return 1;
		}

		std::string verData;
		verData.resize(verSize);

		if (!GetFileVersionInfo(path, verHandle, verSize, verData.data()))
		{
			RAIDHOOK_LOG_ERROR(std::format("Error occurred while calling 'GetFileVersionInfo': {}", GetLastError()));
			lua_pushstring(L, "0.0.0.0");
			return 1;
		}

		if (!VerQueryValue(verData.data(), "\\", (VOID FAR * FAR*)&lpBuffer, &size))
		{
			RAIDHOOK_LOG_ERROR(std::format("Error occurred while calling 'VerQueryValue': {}", GetLastError()));
			lua_pushstring(L, "0.0.0.0");
			return 1;
		}

		if (size == 0)
		{
			RAIDHOOK_LOG_ERROR("Invalid version value buffer Size");
			lua_pushstring(L, "0.0.0.0");
			return 1;
		}

		VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
		if (verInfo->dwSignature != 0xfeef04bd)
		{
			RAIDHOOK_LOG_ERROR("Invalid version signature");
			lua_pushstring(L, "0.0.0.0");
			return 1;
		}

		std::string strVersion = std::format(
			"{}.{}.{}.{}", (verInfo->dwFileVersionMS >> 16) & 0xFFFF, (verInfo->dwFileVersionMS >> 0) & 0xFFFF,
			(verInfo->dwFileVersionLS >> 16) & 0xFFFF, (verInfo->dwFileVersionLS >> 0) & 0xFFFF);

		lua_pushstring(L, strVersion.c_str());
		return 1;
	}
	
	static int luaF_flush_log(lua_State* L)
	{
		Logging::Logger::Instance().flush();

		return 1;
	}

	static int luaF_sd_identify(lua_State* L)
	{
		size_t len;
		const char *data = lua_tolstring(L, 1, &len);

		bool is32bit = raidhook::scriptdata::determine_is_32bit(len, (const uint8_t*)data);

		lua_newtable(L);

		lua_pushboolean(L, is32bit);
		lua_setfield(L, -2, "is32bit");

		return 1;
	}

	static int luaF_sd_recode(lua_State* L)
	{
		size_t len;
		const char *data = lua_tolstring(L, 1, &len);

		if(lua_type(L, 2) != LUA_TTABLE)
		{
			luaL_error(L, "Second argument to blt.scriptdata.recode must be a table");
		}

		lua_getfield(L, 2, "is32bit");
		bool is32bit = lua_toboolean(L, -1);
		lua_pop(L, 1);

		raidhook::scriptdata::ScriptData sd(len, (const uint8_t*)data);
		std::string out = sd.GetRoot()->Serialise(is32bit);

		lua_pushlstring(L, out.c_str(), out.length());
		return 1;
	}

	static void load_scriptdata_library(lua_State* L)
	{
		luaL_Reg items[] =
		{
			{ "identify", luaF_sd_identify },
			{ "recode", luaF_sd_recode },
			{ NULL, NULL }
		};
		lua_newtable(L); // create the scriptdata table
		luaL_register(L, nullptr, items); // name is null, so register everything onto the table on top of the stack
		lua_setfield(L, -2, "scriptdata"); // save it into the blt table
	}

	int updates = 0;

	static void luaF_close(lua_State* L)
	{
		remove_active_state(L);
		lua_close_exe(L);
	}

	void InitiateStates()
	{
		// Set up debugging right away, for log viewing
#ifdef ENABLE_DEBUG
		DebugConnection::Initialize();
#endif

		blt::platform::InitPlatform();
	}

	void DestroyStates()
	{
		blt::platform::ClosePlatform();
	}

}

using namespace raidhook;

namespace blt
{
	namespace lua_functions
	{

		void perform_lua_pcall(lua_State* L, int args, int returns)
		{
			// https://stackoverflow.com/questions/30021904/lua-set-default-error-handler/30022216#30022216
			lua_getglobal(L, "debug");
			if(lua_isnil(L, -1))
			{
				// Debug isn't available, use normal call
				lua_remove(L, -1);
				return lua_call_exe(L, args, returns);
			}
			lua_getfield(L, -1, "traceback");
			lua_remove(L, -2);
			// Do not index from the top (i.e. use a negative index) as this has the potential to mess up if the callee function returns
			// values /and/ lua_pcall() is set up with a > 0 nresults argument
			int errorhandler = lua_gettop(L) - args - 1;
			lua_insert(L, errorhandler);

			int result = lua_pcall(L, args, returns, errorhandler);
			if (result != 0)
			{
				size_t len;
				const char* message = lua_tolstring(L, -1, &len);
				RAIDHOOK_LOG_ERROR(message);
				NotifyErrorOverlay(L, message);
				// This call pops the error message off the stack
				lua_pop(L, 1);
				// No, don't touch this variable anymore
				message = nullptr;
			}
			// This call removes the error handler from the stack. Do not use lua_pop() as the callee function's return values may be
			// present, which would pop one of those instead and leave the error handler on the stack
			lua_remove(L, errorhandler);
		}

		bool setup_check_done = false;

		// Random dude who wrote what's his face?
		// I 'unno, I stole this method from the guy who wrote the 'underground-light-lua-hook'
		// Mine worked fine, but this seems more elegant.
		void initiate_lua(lua_State *L)
		{
			add_active_state(L);

			std::ifstream infileunsafedev("mods/unsafe_developer.txt"); // TODO find better name?
			if (infileunsafedev.good())
			{
				RAIDHOOK_LOG_LOG("Forcing pcalls early!\nPlease backup your save file as this feature is intended for developers only and might break alot of things down the line!");
				luaF_forcepcalls(L);
			}


			if (!setup_check_done)
			{
				setup_check_done = true;

				if (!(Util::DirectoryExists("mods") && Util::DirectoryExists("mods/base")))
				{
					/*int result = MessageBox(NULL,
					                             "Do you want to download the RAID SuperBLT basemod?\n"
					                        "This is required for using mods", "SuperBLT 'mods/base' folder missing", MB_YESNO);
					if (result == IDYES) download_blt();*/
					MessageBox(NULL, "SuperBLT basemod is required to use mods, it will be downloaded now", "SuperBLT basemod required", MB_OK);
					download_blt();
					return;
				}

				if (!Util::DirectoryExists("mods/base/wren"))
				{
					int result = MessageBox(NULL, "It appears you have a vanilla BLT basemod. This is incompatible with SuperBLT.\n"
					                         "Please delete your 'mods/base' folder, and run the game again to automatically download a compatible version",
					                         "BLT basemod outdated", MB_OK);

					exit(1);
					return;
				}
			}

			lua_pushcclosure(L, luaF_print, 0);
			lua_setfield(L, LUA_GLOBALSINDEX, "log");

			lua_pushcclosure(L, luaF_pcall, 0);
			lua_setfield(L, LUA_GLOBALSINDEX, "pcall");

			lua_pushcclosure(L, luaF_dofile, 0);
			lua_setfield(L, LUA_GLOBALSINDEX, "dofile");

			lua_pushcclosure(L, luaF_unzipfile, 0);
			lua_setfield(L, LUA_GLOBALSINDEX, "unzip");

			lua_pushcclosure(L, luaF_dohttpreq, 0);
			lua_setfield(L, LUA_GLOBALSINDEX, "dohttpreq");

			luaL_Reg consoleLib[] =
			{
				/*{ "CreateConsole", luaF_createconsole }, // TODO reenable
				{ "DestroyConsole", luaF_destroyconsole },*/
				{ NULL, NULL }
			};
			luaL_register(L, "console", consoleLib);
			lua_pop(L, 1);

			luaL_Reg fileLib[] =
			{
				{ "GetDirectories", luaF_getdir },
				{ "GetFiles", luaF_getfiles },
				{ "RemoveDirectory", luaF_removeDirectory },
				{ "DirectoryExists", luaF_directoryExists },
				{ "DirectoryHash", luaF_directoryhash },
				{ "FileExists", luaF_fileExists },
				{ "FileHash", luaF_filehash },
				{ "MoveDirectory", luaF_moveDirectory },
				{ "CreateDirectory", luaF_createDirectory },
				{ NULL, NULL }
			};
			luaL_register(L, "file", fileLib);
			lua_pop(L, 1);

			// Keeping everything in lowercase since IspcallForced / IsPCallForced and Forcepcalls / ForcePCalls look rather weird anyway
			luaL_Reg bltLib[] =
			{
				{ "ispcallforced", luaF_ispcallforced },
				{ "forcepcalls", luaF_forcepcalls },
				{ "parsexml", luaF_parsexml },
				{ "structid", luaF_structid },
				{ "ignoretweak", luaF_ignoretweak },
				{ "load_native", luaF_load_native },
				{ "blt_info", luaF_blt_info },
				{ "blt_version", luaF_blt_version },
				{ "flush_log", luaF_flush_log },

				// Functions that are supposed to be in Lua, but are either omitted or implemented improperly (pcall)
				{ "pcall", luaF_pcall_proper }, // Lua pcall shouldn't print errors, however BLT's global pcall does (leave it for compat)
				{ "xpcall", luaF_xpcall },

				{ NULL, NULL }
			};
			luaL_register(L, "blt", bltLib);

			load_scriptdata_library(L);
			load_lua_utils(L);
			load_lua_asset_db(L);
			load_lua_async_io(L);
			raidhook::tweaker::lua_io::register_lua_functions(L);

			lua_pop(L, 1); // pop the BLT library

#ifdef ENABLE_DEBUG
			DebugConnection::AddGlobals(L);
#endif
#ifdef ENABLE_XAUDIO
			xaudio::XAudio::Register(L);
#endif

			for (plugins::Plugin *plugin : plugins::GetPlugins())
			{
				plugin->AddToState(L);
			}

			int result;
			RAIDHOOK_LOG_LOG("Initiating Hook");

			result = luaL_loadfilex(L, "mods/base/base.lua", nullptr);
			if (result == LUA_ERRSYNTAX)
			{
				size_t len;
				RAIDHOOK_LOG_ERROR(lua_tolstring(L, -1, &len));
				return;
			}
			
			result = lua_pcall(L, 0, 0, 0);
			if (result != 0)
			{
				size_t len;
				RAIDHOOK_LOG_ERROR("Failed initializing the basemod:");
				RAIDHOOK_LOG_ERROR(lua_tolstring(L, -1, &len));
				abort();
			}

			return;
		}

		void close(lua_State *L)
		{
			remove_active_state(L);
		}

		void update()
		{
			if (updates == 0)
			{
				RAIDHOOK_LOG_LOG("Checking for updates");
			}

			if (updates > 1)
			{
				EventQueueMaster::GetSingleton().ProcessEvents();
			}

			for (lua_State*& state : activeStates)
			{
#ifdef ENABLE_DEBUG
				DebugConnection::Update(state);
#endif

				for (plugins::Plugin *plugin : plugins::GetPlugins())
				{
					plugin->Update(state);
				}
			}

			updates++;
		}
	};

	void plugins::RegisterPluginForActiveStates(Plugin * plugin)
	{
		for (lua_State *&state : activeStates)
		{
			plugin->AddToState(state);
		}
	}
};
