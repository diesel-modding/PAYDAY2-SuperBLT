#include "http.h"
#include "platform.h"
#include "util/util.h"
#include <curl/curl.h>

#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

static const char* DOWNLOAD_URL = "https://api.modworkshop.net/mods/49758/download";
static const char* OUT_FILE_NAME = "blt_basemod_download.zip";

static size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream)
{
	size_t written = fwrite(ptr, size, nmemb, (FILE*)stream);
	return written;
}

void raidhook::download_blt()
{
	blt::platform::win32::OpenConsole();
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleTextAttribute(hStdout, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	printf("Downloading BLT...\n");

	// HTTP Manager shouldn't have started yet - do this ourselves
	curl_global_init(CURL_GLOBAL_ALL);

	/* init the curl session */
	CURL* curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_URL, DOWNLOAD_URL); // Set the URL to download
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); // Enable the progress meter
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects

	// Put our errors into a buffer
	char errbuf[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	// Open our ZIP file
	FILE* pagefile = NULL;
	errno_t err = fopen_s(&pagefile, OUT_FILE_NAME, "wb");
	if (err != 0)
	{
		/* cleanup curl stuff */
		curl_easy_cleanup(curl);

		SetConsoleTextAttribute(hStdout, FOREGROUND_RED | FOREGROUND_INTENSITY);
		printf("\nError opening output file %s - err %d\n", OUT_FILE_NAME, err);
		MessageBox(0, "An error occured.", "BLT Downloader", MB_OK);
		exit(0);
	}

	// Pass the data into the write_data function, along with the file handle.
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, pagefile);

	// Run the download
	SetConsoleTextAttribute(hStdout, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
	CURLcode cerr = curl_easy_perform(curl);

	// Close the file
	fclose(pagefile);

	// Check for errors
	if (cerr != CURLE_OK)
	{
		SetConsoleTextAttribute(hStdout, FOREGROUND_RED | FOREGROUND_INTENSITY);

		printf("\nError downloading basemod with error %d (URL=%s)\nERR: %s\n", cerr, DOWNLOAD_URL, errbuf);
		MessageBox(0, "An error occured.", "BLT Downloader", MB_OK);
		exit(0);
	}

	/* cleanup curl stuff */
	curl_easy_cleanup(curl);

	SetConsoleTextAttribute(hStdout, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	printf("Download done. Installing...\n");

	CreateDirectory("mods", NULL);
	raidhook::ExtractZIPArchive(OUT_FILE_NAME, "mods");

	SetConsoleTextAttribute(hStdout, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	printf("Cleaning up...\n");

	DeleteFile(OUT_FILE_NAME);

	SetConsoleTextAttribute(hStdout, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
	printf("Installation complete. Please restart the game.\n");

	MessageBox(0, "The installation has finished.\nPlease restart the game.", "SuperBLT Downloader", MB_OK);
	exit(0);
}
