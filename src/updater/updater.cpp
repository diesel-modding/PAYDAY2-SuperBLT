//
// Created by Campbell on 26/07/2026.
//

#include "updater.h"

#include "mxml.h"
#include "platform.h"
#include "updater_pubkey.h"
#include "util/util.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mldsa/mldsa_native.h>
#include <string>
#include <windows.h>

static const char* VERSION_URL = "https://api.modworkshop.net/mods/58342/version";
static const char* DOWNLOAD_URL = "https://api.modworkshop.net/mods/58342/download";

static const char* LAST_UPDATE_PATH = "mods/saves/blt_dll_update_cache.xml";

using namespace raidhook;

using std::filesystem::path;

struct UpdateCheckInfo
{
	int64_t lastCheckTimestamp; // When we last checked for an update
	std::string serverVersion; // The version number advertised by the server
};

enum class UpdateCheckCacheState
{
	UP_TO_DATE, // We've recently checked and found we're up to date
	NEEDS_CHECK, // We were up to date, but haven't checked in awhile
	UPDATE_PENDING, // Last time we checked we found a pending update
};

static int DownloadUrlToString(const char* url, std::string& outVersion);
static int64_t GetTimestamp(); // ms since 1/1/1601
static UpdateCheckCacheState ReadCachedUpdateState();
static std::optional<UpdateCheckInfo> ReadUpdateCheckInfo();
static bool VerifySignature(const uint8_t* data, size_t dataSize, const uint8_t* signature, size_t signatureSize);
static path GetGameDir();

extern "C" __declspec(dllexport) void CALLBACK RUNDLL_DoUpdateCheck(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
                                                                    int nCmdShow)
{
	path gameDir = GetGameDir();

	if (Util::GetFileType("mods/developer.txt") != Util::FileType::FileType_None)
		blt::platform::win32::OpenConsole();

	printf("SuperBLT Update Checker\n");
	printf("Using game directory: '%s'\n", gameDir.string().c_str());

	std::string version;
	int result = DownloadUrlToString(VERSION_URL, version);
	version = Util::StripWhitespace(version);

	if (result != 0)
	{
		printf("Update check failed: %d\n", result);
		ExitProcess(1);
	}

	printf("Remote version: '%s'\n", version.c_str());

	std::string timestamp = std::to_string(GetTimestamp());

	// Save the update information to an XML, which will later be read back into a UpdateCheckInfo struct.
	// We totally could just use a simple binary format, but we already have an XML library linked and it's
	// easier for a human to debug if something weird happens.
	mxml_node_t* tree = mxmlNewXML(nullptr);

	mxml_node_t* root = mxmlNewElement(tree, "update-state");
	mxmlElementSetAttr(root, "last-check", timestamp.c_str());
	mxmlElementSetAttr(root, "server-version", version.c_str());
	mxmlAdd(tree, MXML_ADD_AFTER, nullptr, root);

	const char* s = mxmlSaveAllocString(tree, nullptr);
	// printf("XML: %s\n", s);

	{
		std::ofstream f(gameDir / LAST_UPDATE_PATH, std::ios::binary);
		f.write(s, strlen(s));
	}

	free((void*)s);
}

extern "C" __declspec(dllexport) void CALLBACK RUNDLL_InstallUpdate(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
                                                                    int nCmdShow)
{
	// Always show the console during updates, so we can give the user some feedback
	blt::platform::win32::OpenConsole();
	printf("SuperBLT DLL Updater\n");

	auto printErrorAndExit = []
	{
		printf("Please manually update SuperBLT by downloading a new version from ModWorkshop\n");
		printf("(press enter to continue)\n");
		getc(stdin); // Wait for the user
		ExitProcess(1);
	};

	std::string zipData;
	int result = DownloadUrlToString(DOWNLOAD_URL, zipData);

	if (result != 0)
	{
		printf("Failed to download new DLL ZIP: %d\n", result);
		printErrorAndExit();
	}

	// Extract the ZIP file in RAM
	std::vector<std::unique_ptr<Zip::ZIPFileData>> files =
		Zip::ReadZipFile((const uint8_t*)zipData.data(), zipData.size());

	std::map<std::string, Zip::ZIPFileData*> byName;
	std::string dllName;

	for (const std::unique_ptr<Zip::ZIPFileData>& file : files)
	{
		if (file->filepath.ends_with(".dll"))
			dllName = file->filepath;

		byName[file->filepath] = file.get();
	}

	const std::string& dllData = byName[dllName]->decompressedData;

	// Don't crash if the signature is missing, but leave it empty which we'll pick up on later
	std::string signatureData;
	Zip::ZIPFileData* sigEntry = byName[dllName + ".sig"];
	if (sigEntry)
		signatureData = sigEntry->decompressedData;

	// Check the signature
	bool valid = VerifySignature((const uint8_t*)dllData.data(), dllData.size(), (const uint8_t*)signatureData.data(),
	                             signatureData.size());
	if (!valid)
	{
		printf("Download corrupt (invalid signature), update failed!\n");
		printErrorAndExit();
	}
	printf("Successfully verified digital signature.\n");

	// We can rename open DLLs, but not delete them.
	std::string dllFilename = Util::GetModuleFileNameCxx(blt::THIS_COMPONENT);
	std::string dllFilenameNew = dllFilename + ".new";
	std::string dllFilenameOld = dllFilename + ".old";

	printf("DLL Path: %s\n", dllFilename.c_str());

	// If there's an old file from a previous installation, get rid of it first
	if (Util::GetFileType(dllFilenameOld) != Util::FileType::FileType_None)
	{
		if (remove(dllFilenameOld.c_str()))
		{
			printf("Failed to delete old DLL file from previous auto-update: %lu\n", GetLastError());
			printErrorAndExit();
		}
	}

	// Write out the new file before we start moving any files to reduce the chance of the installation getting broken
	{
		std::ofstream fi(dllFilenameNew, std::ios::binary);
		fi.write(dllData.data(), dllData.size());
		if (fi.fail() | fi.bad())
		{
			printf("Failed to write new DLL file: %lu\n", GetLastError());
			printErrorAndExit();
		}
	}

	// Rename the files around
	if (rename(dllFilename.c_str(), dllFilenameOld.c_str()))
	{
		printf("Failed to rename old DLL file: %lu\n", GetLastError());
		printErrorAndExit();
	}

	// At this point there's no WSOCK32.dll/IPHLPAPI.dll file - if we can't move on from here we've broken the install.

	if (rename(dllFilenameNew.c_str(), dllFilename.c_str()))
	{
		printf("Failed to rename new DLL file: %lu\n", GetLastError());
		printf(
			"The SuperBLT installation may be broken at this point - please note down and report the above error!\n");
		printErrorAndExit();
	}

	// The .old DLL is left sitting around until the next update, but that really doesn't matter - it
	// might be of some help with rolling back updates or whatever, and uses a negligible amount of space.
	// The separate EXE-based update system that RAID BLT uses deletes these files on startup, and we
	// could very easily do that if we wanted to here.

	printf("Update completed successfully!\n");
	printf("(press enter to continue)\n");
	getc(stdin);
}

static size_t WriteDataStream(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	std::ostringstream* stream = (std::ostringstream*)userdata;
	size_t count = size * nmemb;
	stream->write(ptr, count);
	return count;
}

static int DownloadUrlToString(const char* url, std::string& outVersion)
{
	// Init curl
	CURL* curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // debug
	static char errbuf[CURL_ERROR_SIZE] = {};
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	// Check for updates
	std::ostringstream versionStream;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDataStream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &versionStream);
	CURLcode res4 = curl_easy_perform(curl);
	if (res4 != CURLE_OK)
	{
		curl_easy_cleanup(curl);
		return 2;
	}

	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

	if (!(responseCode >= 200 && responseCode < 300))
	{
		curl_easy_cleanup(curl);
		return 3;
	}

	outVersion = std::move(versionStream.str());
	return 0;
}

static std::optional<UpdateCheckInfo> ReadUpdateCheckInfo()
{
	std::string updatePath = (GetGameDir() / LAST_UPDATE_PATH).string();
	FILE* fp = fopen(updatePath.c_str(), "r");
	if (!fp)
	{
		return std::nullopt;
	}

	mxml_node_t* tree = mxmlLoadFile(nullptr, fp, MXML_OPAQUE_CALLBACK);
	fclose(fp);

	std::optional<UpdateCheckInfo> result;

	mxml_node_t* root = nullptr;
	if (tree)
		root = mxmlFindElement(tree, tree, "update-state", nullptr, nullptr, MXML_DESCEND);
	if (root)
	{
		const char* lastCheck = mxmlElementGetAttr(root, "last-check");
		const char* serverVersion = mxmlElementGetAttr(root, "server-version");

		result = UpdateCheckInfo{
			.lastCheckTimestamp = std::stoll(lastCheck),
			.serverVersion = serverVersion,
		};
	}

	mxmlDelete(tree);
	return result;
}

static int64_t GetTimestamp()
{
	SYSTEMTIME st;
	GetSystemTime(&st);
	FILETIME ft;
	SystemTimeToFileTime(&st, &ft); // Assume this succeeds

	ULARGE_INTEGER uli;
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;

	// The filetime interval is in units of 100ns, convert up to milliseconds to make the units nicer.
	uint64_t ms = uli.QuadPart / 10'000;

	// Always use signed times to avoid casting when comparing times.
	return (int64_t)ms;
}

static UpdateCheckCacheState ReadCachedUpdateState()
{
	// Try to read and parse the update info
	std::optional<UpdateCheckInfo> updateInfo = ReadUpdateCheckInfo();
	if (!updateInfo)
		return UpdateCheckCacheState::NEEDS_CHECK;

	if (updateInfo->serverVersion != blt::SBLT_VERSION)
	{
		return UpdateCheckCacheState::UPDATE_PENDING;
	}

	// Have we checked for updates in the last hour?
	// If the elapsed time since the last check is negative, then something funny
	// happened with the clocks (set wrong either before or now), and we should check again.
	int64_t elapsedMS = GetTimestamp() - updateInfo->lastCheckTimestamp;
	if (elapsedMS < 0 || elapsedMS > 3600 * 1000)
		return UpdateCheckCacheState::NEEDS_CHECK;

	return UpdateCheckCacheState::UP_TO_DATE;
}

// timeoutMS can be INFINITE
// Returns true if we launched the process and it exited within the timeout
static bool RunUpdateProcess(std::string commandLine, const std::string& startDir, DWORD timeoutMS)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// commandLine has to be a C++ string, as CreateProcess will modify the pointer we give it

	// Start the child process.
	if (!CreateProcessA(nullptr, // No module name (use command line)
	                    commandLine.data(), // Command line
	                    nullptr, // Process handle not inheritable
	                    nullptr, // Thread handle not inheritable
	                    FALSE, // Set handle inheritance to FALSE
	                    0, // No creation flags
	                    nullptr, // Use parent's environment block
	                    startDir.c_str(), // Use parent's starting directory
	                    &si, // Pointer to STARTUPINFO structure
	                    &pi) // Pointer to PROCESS_INFORMATION structure
	)
	{
		std::string msg = std::format("Update check: CreateProcess failed ({})", GetLastError());
		RAIDHOOK_LOG_WARN(msg);
		return false;
	}

	// Wait until child process exits, or our timeout elapses.
	DWORD state = WaitForSingleObject(pi.hProcess, timeoutMS);

	// Close process and thread handles.
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	// Did the process exit before the timeout?
	return state == WAIT_OBJECT_0;
}

void raidhook::CheckForUpdates()
{
	// This would get *really* annoying if you're working on the DLL
	if (Util::GetFileType("mods/disable dll updates.txt") != Util::FileType::FileType_None)
		return;

	// Have we already checked recently?
	// This will avoid wasting the user's time if they're repeatedly re-launching the game
	// and have slow internet.
	UpdateCheckCacheState state = ReadCachedUpdateState();
	if (state == UpdateCheckCacheState::UP_TO_DATE)
	{
		RAIDHOOK_LOG_LOG("Recently checked for updates, skipping");
		return;
	}

	// Launch a rundll32 process to load the EXE this very code lives in. This lets us run curl
	// during startup without worrying about the LoadLibrary lock.

	// rundll doesn't handle paths with spaces, so run the DLL from the working directory it lives in
	std::string dllFilename = Util::GetModuleFileNameCxx(blt::THIS_COMPONENT);
	path dllPath = dllFilename;
	path gameDir = dllPath.parent_path();
	std::string dllName = dllPath.filename().string();

	std::string commandLine = std::format("rundll32.exe .\\{},RUNDLL_DoUpdateCheck", dllName);

	// If we found there was an update pending last time we checked, check again just in case
	// it was rolled back or something. Don't time out though.
	// If we have no reason to believe there's an update, don't hold up the player's game if
	// they have particularly slow internet. We'll find out next time they restart the game.
	DWORD timeoutMS = state == UpdateCheckCacheState::UPDATE_PENDING ? INFINITE : 1500;

	RAIDHOOK_LOG_LOG("Checking for updates");
	bool finished = RunUpdateProcess(commandLine, gameDir.string(), timeoutMS);
	if (!finished)
	{
		RAIDHOOK_LOG_WARN("Update check timed out");
		return;
	}

	// Read the file again, and see if we've now found an update.
	if (ReadCachedUpdateState() != UpdateCheckCacheState::UPDATE_PENDING)
		return;

	int promptResult = MessageBox(nullptr,
	                              "A SuperBLT DLL update is available."
	                              "\nDo you want to automatically install the update?",
	                              "SuperBLT Updater", MB_YESNO);
	if (promptResult != IDYES)
		return;

	RAIDHOOK_LOG_WARN("Found update, installing it!");

	// If so, go ahead and install it
	commandLine = std::format("rundll32.exe .\\{},RUNDLL_InstallUpdate", dllName);
	finished = RunUpdateProcess(commandLine, gameDir.string(), INFINITE);

	if (!finished)
	{
		MessageBox(nullptr, "Failed to install update. Please manually update your SuperBLT DLL.", "SuperBLT", MB_OK);

		// Let the game start normally. Obviously we'd like players to always update, but don't break
		// their game if something weird is happening with the updater.
	}
	else
	{
		// Restart the game after installing the update to get the new DLL.
		MessageBox(nullptr, "Restart the game to complete the update process.", "SuperBLT", MB_OK);
		ExitProcess(0);
	}
}

static bool VerifySignature(const uint8_t* data, size_t dataSize, const uint8_t* signature, size_t signatureSize)
{
	static_assert(sizeof(TESTING_PUBKEY) == MLDSA65_PUBLICKEYBYTES);
	static_assert(sizeof(PRODUCTION_PUBKEY) == MLDSA65_PUBLICKEYBYTES);

	if (signatureSize != MLDSA65_BYTES)
		return false;

	int result = PQCP_MLDSA_NATIVE_MLDSA65_verify(signature, data, dataSize, nullptr, 0, PRODUCTION_PUBKEY);

	return result == 0;
}

static path GetGameDir()
{
	std::string dllFilename = Util::GetModuleFileNameCxx(blt::THIS_COMPONENT);
	path dllPath = dllFilename;
	path gameDir = dllPath.parent_path();
	return gameDir;
}
