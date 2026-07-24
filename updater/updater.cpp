#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <format>
#include <iostream>
#include <filesystem>

#include "compression.h"

static const char *DLL_UPDATE_FILE = "sblt_dll.zip";
static const char *UPDATER_FILE = "sblt_updater.zip";

static const char *DOWNLOAD_URL_DLL_UPDATER = "https://api.modworkshop.net/mods/54109/download";
static const char *DOWNLOAD_URL_DLL_WSOCK32 = "https://api.modworkshop.net/mods/49746/download";
static const char *DOWNLOAD_URL_DLL_IPHLPAPI = "https://api.modworkshop.net/mods/49745/download";

static const char *VERSION_URL_DLL_UPDATER = "https://api.modworkshop.net/mods/54109/version";
static const char *VERSION_URL_DLL_WSOCK32 = "https://api.modworkshop.net/mods/49746/version";
static const char *VERSION_URL_DLL_IPHLPAPI = "https://api.modworkshop.net/mods/49745/version";

static const int UPDATER_VERSION = 1;

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
	size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
	return written;
}

size_t write_data_stream(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::ostringstream *stream = (std::ostringstream*)userdata;
    size_t count = size * nmemb;
    stream->write(ptr, count);
    return count;
}

int main(int argc, char *argv[])
{
    /* return codes:
        0: no update found/done
        1: update successful
        2: error while updating
		3: self update of updater
    */
    if (argc < 2)
    {
		// no local_version given
		MessageBox(NULL, "SBLT_DLL_UPDATER is coded to be started from within SBLT DLL", "SBLT DLL Downloader", MB_OK);
        return 2;
    }
    
	// init curl
	curl_global_init(CURL_GLOBAL_ALL);
	// check which dll is used
	std::string DLL = "";
	std::string DLL_old = "";
	std::ifstream infile_iphlpapi("../IPHLPAPI.dll");
	std::ifstream infile_wsock32("../WSOCK32.dll");
	std::ifstream infile_debug_updater("../mods/debug_updater.txt");
	std::ostringstream datastream;
	std::ostringstream selfupdate_rver;
    std::string URL;

	
	if (infile_iphlpapi.good() and infile_wsock32.good())
	{
		// both dlls present
		return 2;
	}

	if (infile_iphlpapi.good())
	{
		DLL = "../IPHLPAPI.dll";
		DLL_old = "../IPHLPAPI.dll.old";
	}
	if (infile_wsock32.good())
	{
		DLL = "../WSOCK32.dll";
		DLL_old = "../WSOCK32.dll.old";
	}

	if (DLL == "")
	{
		// no dll could be detected
		return 2;
	}

	// remove left over old files
	if (std::filesystem::exists(DLL_old.c_str()))
	{
		std::filesystem::remove(DLL_old.c_str());
	}
	if (std::filesystem::exists("SBLT_DLL_UPDATER.exe.old"))
	{
		std::filesystem::remove("SBLT_DLL_UPDATER.exe.old");
	}

	// init curl
	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // debug
	char errbuf[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	// SELF: check for updates
	curl_easy_setopt(curl, CURLOPT_URL, VERSION_URL_DLL_UPDATER);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data_stream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &selfupdate_rver);
	CURLcode res4 = curl_easy_perform(curl);
	if (res4 != CURLE_OK)
	{
		curl_easy_cleanup(curl);
		return 2;
	}

	int selfupdate_remote_version = 0;
	std::string selfupdate_rver_str = selfupdate_rver.str();

	sscanf(selfupdate_rver_str.c_str() , "%d", &selfupdate_remote_version);

	// SELF: compare versions
	bool self_update = false;
	if (UPDATER_VERSION < selfupdate_remote_version or infile_debug_updater.good())
	{
		// ask user to update dll updater
		int self_result = MessageBox(NULL, "Do you want to update the RAID SuperBLT DLL Updater?\nThis is recommended.", "SuperBLT DLL Updater out of date", MB_YESNO);
		if (self_result == IDYES){
			self_update = true;
		}
	}

	// SELF: update updater
	if (self_update == true)
	{
		// download new updater
		curl_easy_setopt(curl, CURLOPT_URL, DOWNLOAD_URL_DLL_UPDATER);
		FILE *pagefile2 = NULL;
		errno_t err2 = fopen_s(&pagefile2, UPDATER_FILE, "wb");
		if (err2 != 0)
		{
			/* cleanup curl stuff */
			curl_easy_cleanup(curl);
			printf("\nError opening output file %s - err %d\n", DLL_UPDATE_FILE, err2);
			MessageBox(0, "An error occured.", "SBLT DLL Downloader", MB_OK);
			return 2;
		}

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, pagefile2);
		CURLcode res3 = curl_easy_perform(curl);

		fclose(pagefile2);

		// rename current updater
		if (std::filesystem::exists("SBLT_DLL_UPDATER.exe"))
		{
			std::filesystem::rename("SBLT_DLL_UPDATER.exe", "SBLT_DLL_UPDATER.exe.old");
		}

		// DLL: unpack new dll
		raidhook::ExtractZIPArchive(UPDATER_FILE, ".");
		// DLL: clean up
		std::filesystem::remove(UPDATER_FILE);
		curl_easy_cleanup(curl);

		// DLL: tell user to restart game
		MessageBox(0, "SuperBLT DLL Updater was updated successfully.\nPlease restart your game.", "SuperBLT DLL Updater", MB_OK);
		return 3;

	}

	
	// DLL: check for updates
	// DLL: get remote version

	if (DLL == "../IPHLPAPI.dll")
	{
		curl_easy_setopt(curl, CURLOPT_URL, VERSION_URL_DLL_IPHLPAPI);
	}
	else
	{
		curl_easy_setopt(curl, CURLOPT_URL, VERSION_URL_DLL_WSOCK32);
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data_stream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &datastream);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK)
	{
		curl_easy_cleanup(curl);
		return 2;
	}

	std::string remote_version = datastream.str();

	// DLL: get local version
	std::string local_version;
	if (infile_debug_updater.good())
	{
		local_version = "0.0.0.0";
	}
	else
	{
		local_version = argv[1];
	}

	// DLL: compare versions
	int lVerMaj = 0;
	int lVerMin = 0;
	int lVerPatch = 0;
	int lVerRev = 0;
	int rVerMaj = 0;
	int rVerMin = 0;
	int rVerPatch = 0;
	int rVerRev = 0;
	bool newer = false;

	// DLL: parse version strings into ints
	sscanf(local_version.c_str(), "%d.%d.%d.%d", &lVerMaj, &lVerMin, &lVerPatch, &lVerRev);
	sscanf(remote_version.c_str(), "%d.%d.%d.%d", &rVerMaj, &rVerMin, &rVerPatch, &rVerRev);

	if (rVerMaj > lVerMaj)
	{
		newer = true;
	}
	if (rVerMin > lVerMin and newer == false)
	{
		newer = true;
	}
	if (rVerPatch > lVerPatch and newer == false)
	{
		newer = true;
	}
	if (rVerRev > lVerRev and newer == false)
	{
		newer = true;
	}

	// DLL: download new dll
	if (newer == true)
	{
		// DLL: ask user to update
		int result = MessageBox(NULL, "Do you want to update the RAID SuperBLT DLL?\nThis is recommended.", "SuperBLT DLL out of date", MB_YESNO);
		if (result == IDNO){
			curl_easy_cleanup(curl);
			return 0;
		}
		if (DLL == "../IPHLPAPI.dll")
		{
			curl_easy_setopt(curl, CURLOPT_URL, DOWNLOAD_URL_DLL_IPHLPAPI);
            URL = DOWNLOAD_URL_DLL_IPHLPAPI;
		}
		else
		{
			curl_easy_setopt(curl, CURLOPT_URL, DOWNLOAD_URL_DLL_WSOCK32);
            URL = DOWNLOAD_URL_DLL_WSOCK32;
		}

		FILE *pagefile = NULL;
		errno_t err = fopen_s(&pagefile, DLL_UPDATE_FILE, "wb");
		if (err != 0)
		{
			/* cleanup curl stuff */
			curl_easy_cleanup(curl);
			printf("\nError opening output file %s - err %d\n", DLL_UPDATE_FILE, err);
			MessageBox(0, "An error occured.", "SBLT DLL Downloader", MB_OK);
			return 2;
		}

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, pagefile);
		CURLcode res2 = curl_easy_perform(curl);

		fclose(pagefile);

		if (res2 != CURLE_OK)
		{
			printf("\nError downloading SBLT DLL with error %d (URL=%s)\nERR: %s\n", res2, URL.c_str(), errbuf);
			MessageBox(0, "An error occured.", "SBLT DLL Downloader", MB_OK);
			return 2;
		}

		// DLL: unpack new dll
		raidhook::ExtractZIPArchive(DLL_UPDATE_FILE, ".");
		// DLL: clean up
		std::filesystem::remove(DLL_UPDATE_FILE);
		curl_easy_cleanup(curl);

		// DLL: tell user to restart game
		MessageBox(0, "SuperBLT DLL was updated successfully.\nPlease restart your game.", "SuperBLT DLL Updater", MB_OK);
		return 1;

	}

	/* cleanup curl stuff */
	curl_easy_cleanup(curl);
	return 0;
}
