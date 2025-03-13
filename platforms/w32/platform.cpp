#include <vector>
#define INCLUDE_TRY_OPEN_FUNCTIONS

#include "platform.h"

#include "lua.h"
#include "lua_functions.h"

#include "assets/assets.h"
#include "console/console.h"
#include "signatures/signatures.h"
#include "tweaker/xmltweaker.h"
#include "vr/vr.h"

#include "subhook.h"
#include "util/util.h"

#include <fstream>
#include <string>
#include <thread>

using namespace std;
using namespace pd2hook;

static CConsole* console = NULL;

static std::thread::id main_thread_id;

blt::idstring *blt::platform::last_loaded_name = idstring_none, *blt::platform::last_loaded_ext = idstring_none;

static subhook::Hook gameUpdateDetour, newStateDetour, luaCloseDetour, node_from_xmlDetour;

// This is a very old anti-debug check, from before Overkill/Starbreeze cared about modding.
// It's nice to be able to attach a debugger to figure out where something went wrong.
#ifdef _DEBUG
	subhook::Hook NtSetInformationThreadHook;
	subhook::Hook NtQueryInformationProcessHook;

	// See https://doxygen.reactos.org/d8/d22/ntoskrnl_2ps_2query_8c.html#ae39720dde0849390adeac6c9439aa47d
	enum THREADINFOCLASS : uint32_t
	{
		ThreadHideFromDebugger = 0x11,
	};
	enum PROCESSINFOCLASS : uint32_t
	{
		ProcessDebugFlags = 0x1f,
	};
	NTSTATUS(NTAPI* NtSetInformationThread)
	(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength);
	NTSTATUS(NTAPI* NtQueryInformationProcess)
	(HANDLE ProcessHandle, PROCESSINFOCLASS ProcessInformationClass, PVOID ProcessInformation,
	ULONG ProcessInformationLength, PULONG ReturnLength);

	NTSTATUS NTAPI NtSetInformationThreadFn(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass,
											PVOID ThreadInformation, ULONG ThreadInformationLength);
	NTSTATUS NTAPI NtQueryInformationProcessFn(HANDLE ProcessHandle, PROCESSINFOCLASS ProcessInformationClass,
											PVOID ProcessInformation, ULONG ProcessInformationLength,
											PULONG ReturnLength);
#endif

static void init_idstring_pointers()
{
	char* tmp;

	if (try_open_functions.empty())
	{
		PD2HOOK_LOG_WARN("Could not init idstring pointers because no asset resolver functions were found.");
		return;
	}

	tmp = (char*)try_open_functions.at(0);
	tmp += 0x63;
	blt::platform::last_loaded_name = *((blt::idstring**)tmp);

	tmp = (char*)try_open_functions.at(0);
	tmp += 0x53;
	blt::platform::last_loaded_ext = *((blt::idstring**)tmp);
}

static int __fastcall luaL_newstate_new(void* thislol, int edx, char no, char freakin, int clue)
{
	subhook::ScopedHookRemove scoped_remove(&newStateDetour);

	int ret = luaL_newstate(thislol, no, freakin, clue);

	lua_State* L = (lua_State*)*((void**)thislol);
	// printf("Lua State: %p\n", (void*)L);
	if (!L)
		return ret;

	blt::lua_functions::initiate_lua(L);

	return ret;
}

void* __fastcall do_game_update_new(void* thislol, int edx, int* a, int* b)
{
	subhook::ScopedHookRemove scoped_remove(&gameUpdateDetour);

	// If someone has a better way of doing this, I'd like to know about it.
	// I could save the this pointer?
	// I'll check if it's even different at all later.
	if (std::this_thread::get_id() != main_thread_id)
	{
		return do_game_update(thislol, a, b);
	}

	lua_State* L = (lua_State*)*((void**)thislol);

	blt::lua_functions::update(L);

	return do_game_update(thislol, a, b);
}

void lua_close_new(lua_State* L)
{
	subhook::ScopedHookRemove scoped_remove(&luaCloseDetour);

	blt::lua_functions::close(L);
	lua_close(L);
}

//////////// Start of XML tweaking stuff

static void __fastcall edit_node_from_xml_hook(int arg);

static void __cdecl node_from_xml_new(void* node, char* data, int* len)
{
	char* modded = pd2hook::tweaker::tweak_pd2_xml(data, *len);
	int modLen = *len;

	if (modded != data)
	{
		modLen = strlen(modded);
	}

	edit_node_from_xml_hook(false);
	node_from_xml(node, modded, &modLen);
	edit_node_from_xml_hook(true);

	pd2hook::tweaker::free_tweaked_pd2_xml(modded);
}

static void __fastcall edit_node_from_xml_hook(int arg)
{
	if (arg)
	{
		node_from_xmlDetour.Install(node_from_xml, node_from_xml_new);
	}
	else
	{
		node_from_xmlDetour.Remove();
	}
}

//////////// End of XML tweaking stuff

void blt::platform::InitPlatform()
{
	main_thread_id = std::this_thread::get_id();

	// Set up logging first, so we can see messages from the signature search process
#ifdef INJECTABLE_BLT
	gbl_mConsole = new CConsole();
#else
	ifstream infile("mods/developer.txt");
	string debug_mode;
	if (infile.good())
	{
		debug_mode = "post"; // default value
		infile >> debug_mode;
	}
	else
	{
		debug_mode = "disabled";
	}

	if (debug_mode != "disabled")
		console = new CConsole();
#endif

	SignatureSearch::Search();

	gameUpdateDetour.Install(do_game_update, do_game_update_new);
	newStateDetour.Install(luaL_newstate, luaL_newstate_new);
	luaCloseDetour.Install(lua_close, lua_close_new);

	edit_node_from_xml_hook(true);

	VRManager::CheckAndLoad();
	blt::win32::InitAssets();

	init_idstring_pointers();

	// Get these functions the same way PD2 does, from ntdll.
#ifdef _DEBUG
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	NtSetInformationThread = (decltype(NtSetInformationThread))GetProcAddress(ntdll, "NtSetInformationThread");
	NtQueryInformationProcess = (decltype(NtQueryInformationProcess))GetProcAddress(ntdll, "NtQueryInformationProcess");

	NtSetInformationThreadHook.Install((void*)NtSetInformationThread, (void*)NtSetInformationThreadFn);
	NtQueryInformationProcessHook.Install((void*)NtQueryInformationProcess, (void*)NtQueryInformationProcessFn);
#endif
}

void blt::platform::ClosePlatform()
{
	// Okay... let's not do that.
	// I don't want to keep this in memory, but it CRASHES THE SHIT OUT if you delete this after all is said and done.
	if (console)
		delete console;
}

void blt::platform::GetPlatformInformation(lua_State* L)
{
	lua_pushstring(L, "mswindows");
	lua_setfield(L, -2, "platform");

	lua_pushstring(L, "arch");
	lua_setfield(L, -2, "x86");
}

void blt::platform::win32::OpenConsole()
{
	if (!console)
	{
		console = new CConsole();
	}
}

void* blt::platform::win32::get_lua_func(const char* name)
{
	// Only allow getting the Lua functions
	if (strncmp(name, "lua", 3))
		return NULL;

	// Don't allow getting the setup functions
	if (!strncmp(name, "luaL_newstate", 13))
		return NULL;

	return SignatureSearch::GetFunctionByName(name);
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
		luaCallDetour.Install(lua_call, blt::lua_functions::perform_lua_pcall);
		// PD2HOOK_LOG_LOG("blt.forcepcalls(): Protected calls will now be forced");
	}
	else
	{
		luaCallDetour.Remove();
		// PD2HOOK_LOG_LOG("blt.forcepcalls(): Protected calls are no longer being forced");
	}
}

#ifdef _DEBUG
	NTSTATUS NTAPI NtSetInformationThreadFn(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass,
											PVOID ThreadInformation, ULONG ThreadInformationLength)
	{
		if (ThreadInformationClass == ThreadHideFromDebugger && ThreadInformation == nullptr &&
			ThreadInformationLength == 0 && ThreadHandle == GetCurrentThread())
		{
			// If this function fails, the game skips it's main logic.
			return 0;
		}

		subhook::ScopedHookRemove shr(&NtSetInformationThreadHook);
		return NtSetInformationThread(ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength);
	}

	NTSTATUS NTAPI NtQueryInformationProcessFn(HANDLE ProcessHandle, PROCESSINFOCLASS ProcessInformationClass,
											PVOID ProcessInformation, ULONG ProcessInformationLength,
											PULONG ReturnLength)
	{
		if (ProcessInformationClass == ProcessDebugFlags && ProcessInformationLength == 4 && ReturnLength == nullptr &&
			ProcessHandle == GetCurrentProcess())
		{
			// Without this, the main loop exits from Application::update
			*(uint32_t*)ProcessInformation = 1;
			return 0;
		}

		subhook::ScopedHookRemove shr(&NtQueryInformationProcessHook);
		return NtQueryInformationProcess(ProcessHandle, ProcessInformationClass, ProcessInformation,
										ProcessInformationLength, ReturnLength);
	}
#endif
