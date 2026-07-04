// WINDOWS ONLY FILE!
// Some other stuff might be aswell, but this is rather desperately windows only.

#include "util/util.h"
#include <Windows.h>
#include <stdio.h>
#include <strsafe.h>
#include <tchar.h>

#include <fstream>
#include <streambuf>

using namespace std;

namespace raidhook
{
	namespace Util
	{
		IOException::IOException(const char* file, int line) : Exception(file, line)
		{
		}

		IOException::IOException(std::string msg, const char* file, int line) : Exception(std::move(msg), file, line)
		{
		}

		const char* IOException::exceptionName() const
		{
			return "An IOException";
		}

		vector<string> GetDirectoryContents(const std::string& path, bool dirs)
		{
			vector<string> files;
			WIN32_FIND_DATA ffd;
			TCHAR szDir[MAX_PATH];
			TCHAR* fPath = (TCHAR*)path.c_str();
			// WideCharToMultiByte(CP_ACP, 0, ffd.cFileName, -1, fileName, MAX_PATH, &DefChar, NULL);

			size_t length_of_arg;
			HANDLE hFind = INVALID_HANDLE_VALUE;
			DWORD dwError = 0;
			StringCchLength(fPath, MAX_PATH, &length_of_arg);
			if (length_of_arg > MAX_PATH - 3)
			{
				RAIDHOOK_THROW_IO_MSG("Path too long");
			}
			StringCchCopy(szDir, MAX_PATH, fPath);
			StringCchCat(szDir, MAX_PATH, TEXT("\\*"));

			hFind = FindFirstFile(szDir, &ffd);
			if (hFind == INVALID_HANDLE_VALUE)
			{
				RAIDHOOK_THROW_IO_MSG("FindFirstFile() failed");
			}
			do
			{
				bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

				if ((dirs && isDir) || (!dirs && !isDir))
				{
					files.push_back(ffd.cFileName);
				}
			} while (FindNextFile(hFind, &ffd) != 0);

			dwError = GetLastError();
			if (dwError != ERROR_NO_MORE_FILES)
			{
				RAIDHOOK_THROW_IO_MSG("FindNextFile() failed");
			}
			FindClose(hFind);
			return files;
		}

		bool IsSymlink(const std::string& path)
		{
			DWORD dwAttrib = GetFileAttributes(path.c_str());

			if (dwAttrib == FILE_ATTRIBUTE_REPARSE_POINT)
			{
				return true;
			}
			return false;
		}

		bool DirectoryExists(const std::string& dir)
		{
			string clean = dir;

			if (clean[clean.length() - 1] == '/')
			{
				clean.erase(clean.end() - 1);
			}

			DWORD ftyp = GetFileAttributes(clean.c_str());
			if (ftyp == INVALID_FILE_ATTRIBUTES)
				return false;
			if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
				return true;
			return false;
		}

		bool RemoveEmptyDirectory(const std::string& dir)
		{
			return RemoveDirectory(dir.c_str()) != 0;
		}

		bool CreateDirectorySingle(const std::string& path)
		{
			return CreateDirectory(path.c_str(), NULL);
		}

		FileType GetFileType(const std::string& path)
		{
			DWORD dwAttrib = GetFileAttributes(path.c_str());

			if (dwAttrib == INVALID_FILE_ATTRIBUTES)
			{
				return FileType_None;
			}
			else if (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)
			{
				return FileType_Directory;
			}
			else
			{
				return FileType_File;
			}
		}

		bool MoveDirectory(const std::string& path, const std::string& destination)
		{
			bool success = MoveFileEx(path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
			if (!success)
			{
				RAIDHOOK_LOG_LOG("MoveFileEx failed with error " << GetLastError());
			}
			return success;
		}

	} // namespace Util
} // namespace raidhook
