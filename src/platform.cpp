#include "platform.h"

#include "assets/assets.h"
#include "console/console.h"
#include "signatures/signatures.h"
#include "updater/updater.h"
#include "util/util.h"

#include <windows.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdlib.h>
#include <string>

using namespace std;
using namespace raidhook;

static CConsole* console = NULL;
blt::idstring *blt::platform::last_loaded_name = idstring_none, *blt::platform::last_loaded_ext = idstring_none;

#include <platform_game.cpp>

void blt::platform::InitPlatform()
{
	// Set up logging first, so we can see messages from the signature search process
	ifstream infileconsole("mods/developer.txt");
	string debug_mode;
	if (infileconsole.good())
	{
		debug_mode = "post"; // default value
		infileconsole >> debug_mode;
	}
	else
	{
		debug_mode = "disabled";
	}

	if (debug_mode != "disabled")
		console = new CConsole();

	// remove left over dll if it exists
	if (std::filesystem::exists("WSOCK32.dll.old"))
	{
		std::filesystem::remove("WSOCK32.dll.old");
	}
	if (std::filesystem::exists("IPHLPAPI.dll.old"))
	{
		std::filesystem::remove("IPHLPAPI.dll.old");
	}

	// run external dll updater
	CheckForUpdates();

	if (!SignatureSearch::Search())
	{
		MessageBox(nullptr,
		           "This SuperBLT version is not compatible with your current game version. The game will be started "
		           "without SuperBLT.",
		           "SuperBLT version incompatible", MB_OK);

		if (console)
			console->Close(true);
		return;
	}

	blt::win32::InitAssets();

	setup_platform_game();
}

void blt::platform::ClosePlatform()
{
	// Okay... let's not do that.
	// I don't want to keep this in memory, but it CRASHES THE SHIT OUT if you delete this after all is said and done.
	delete console;
}

void blt::platform::GetPlatformInformation(lua_State* L)
{
	lua_pushstring(L, "win64");
	lua_setfield(L, -2, "platform");

	lua_pushstring(L, "x86-64");
	lua_setfield(L, -2, "arch");

	// lua_pushstring(L, "raid");
	lua_pushstring(L, "pd2");
	lua_setfield(L, -2, "game");
}

void blt::platform::win32::OpenConsole()
{
	if (!console)
	{
		console = new CConsole();
	}
}

subhook::Hook luaCallDetour;

bool blt::platform::lua::GetForcePCalls()
{
	return luaCallDetour.IsInstalled();
}

void blt::platform::lua::SetForcePCalls(bool state)
{
	// Don't change if already set up
	if (state == GetForcePCalls())
		return;

	if (state)
	{
		luaCallDetour.Install(lua_call_exe, blt::lua_functions::perform_lua_pcall);
		RAIDHOOK_LOG_LOG("blt.forcepcalls(): Protected calls will now be forced");
	}
	else
	{
		luaCallDetour.Remove();
		RAIDHOOK_LOG_LOG("blt.forcepcalls(): Protected calls are no longer being forced");
	}
}
