#define WIN32_LEAN_AND_MEAN 1
#include "InitState.h"
#include "util/util.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <windows.h>

#include "loader_asm.gen.h"

using namespace raidhook;

// Thank you, Raymond Chen
// https://devblogs.microsoft.com/oldnewthing/20041025-00/?p=37483
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE) & __ImageBase)

// Used by the generated assembly file, this contains pointers to every function we have to proxy
extern "C" ProxyFunctionTable SBLT_PROXY_STRUCT_PTR = CreateInitialProxyFunctionTable();
extern "C" void SBLT_PROXY_LOADER_FN_CXX(uint64_t functionAndDllId);

static std::mutex dllLoadingMutex;
static bool isWsockLoaded = false;
static bool isIphlpapiLoaded = false;

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hInst);

		// Check if we're being loaded into PD2 - if not just silently exit.
		// This is important if we're being loaded via rundll32 during an update.
		std::string exeFilename = Util::GetModuleFileNameCxx(nullptr);
		if (!exeFilename.ends_with("PAYDAY2.exe"))
			return TRUE;

		std::wstring cmd_line = GetCommandLineW();
		auto payday2_start = cmd_line.find(L"PAYDAY2.exe");

		// "--safemode" or "-sm" is also an engine-sided launch argument that ignores loading mod_overrides to prevent crashes
		if (cmd_line.find(L"--safemode", payday2_start) != std::wstring::npos ||
		    cmd_line.find(L"-sm", payday2_start) != std::wstring::npos)
		{
			return TRUE;
		}

		// Make sure there aren't two copies of SBLT installed
		if (Util::GetFileType("WSOCK32.dll") == Util::FileType_File &&
		    Util::GetFileType("IPHLPAPI.dll") == Util::FileType_File)
		{
			MessageBoxA(nullptr,
			            "You have both SuperBLT DLLs installed - IPHLPAPI.dll and \nWSOCK32.dll. "
			            "Please delete one (preferrably, delete IPHLPAPI.dll).",
			            "Both SuperBLT DLLs installed!", MB_OK);
			ExitProcess(1);
		}

		InitiateStates();
	}
	if (reason == DLL_PROCESS_DETACH)
	{
		// It would in principal be nice to unload the networking library, but MSDN forbids FreeLibrary from DllMain.
		// (it works fine in older versions of SBLT, but there's no real reason to free it anyway)
	}

	return TRUE;
}

void SBLT_PROXY_LOADER_FN_CXX(uint64_t functionAndDllId)
{
	uint32_t functionId = functionAndDllId & 0xFFFFFFFF;
	uint32_t dllId = functionAndDllId >> 32;

	// Make sure that if multiple threads call the function concurrently, we still only load the DLL once
	std::lock_guard guard(dllLoadingMutex);

	char bufd[200];
	GetSystemDirectory(bufd, 200);
	std::string dllName = dllId == DLL_ID_WSOCK32 ? "WSOCK32.dll" : "IPHLPAPI.dll";
	std::string dllPath = std::string(bufd) + "\\" + dllName;

	HMODULE hL = LoadLibrary(dllPath.c_str());
	if (!hL)
	{
		std::string message = "Failed to load system " + dllName;
		MessageBoxA(nullptr, message.c_str(), "SuperBLT Loader", MB_OK);
		ExitProcess(1);
	}

	// Load the addresses for all the functions
	if (dllId == DLL_ID_WSOCK32)
	{
		LoadDllFunctionsWSOCK32(&SBLT_PROXY_STRUCT_PTR, hL);
		isWsockLoaded = true;
	}
	else
	{
		LoadDllFunctionsIPHLPAPI(&SBLT_PROXY_STRUCT_PTR, hL);
		isIphlpapiLoaded = true;
	}

	// std::string message = "A SuperBLT proxied function was called without initialisation. Please report this.\n";
	// message += "Function: " + std::string(FUNCTION_NAMES[functionId]);
	// MessageBoxA(nullptr, message.c_str(), "Bad Proxy Function", MB_OK);
	// abort();
}
