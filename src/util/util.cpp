#include "util.h"
#include <algorithm>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

namespace raidhook
{
	namespace Util
	{

		Exception::Exception(const char* file, int line) : mFile(file), mLine(line)
		{
		}

		Exception::Exception(std::string msg, const char* file, int line)
			: mFile(file), mLine(line), mMsg(std::move(msg))
		{
		}

		const char* Exception::what() const throw()
		{
			if (!mMsg.empty())
			{
				return mMsg.c_str();
			}

			return std::exception::what();
		}

		const char* Exception::exceptionName() const
		{
			return "An exception";
		}

		void Exception::writeToStream(std::ostream& os) const
		{
			os << exceptionName() << " occurred @ (" << mFile << ':' << mLine << "). " << what();
		}

		// helper function to print the digest bytes as a hex string
		std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes)
		{
			std::ostringstream stream;
			for (uint8_t b : bytes)
			{
				stream << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(b);
			}
			return stream.str();
		}

		// Perform SHA-256 hash using Windows CNG API
		std::string sha256(const std::string& input)
		{
			BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
			BCRYPT_HASH_HANDLE hHash = nullptr;
			NTSTATUS status;
			DWORD hashObjectLength = 0, resultLength = 0;
			DWORD hashLength = 32;
			std::vector<uint8_t> hash(hashLength);
			std::vector<uint8_t> hashObject;

			status = BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
			if (status != 0)
				throw std::runtime_error("Failed to open SHA-256 algorithm provider");
			try
			{
				status = BCryptGetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjectLength, sizeof(DWORD),
				                           &resultLength, 0);
				if (status != 0)
					throw std::runtime_error("Failed to get hash object length");
				hashObject.resize(hashObjectLength);
				status = BCryptCreateHash(hAlgorithm, &hHash, hashObject.data(), hashObjectLength, nullptr, 0, 0);
				if (status != 0)
					throw std::runtime_error("Failed to create hash");
				try
				{
					status = BCryptHashData(hHash, (PUCHAR)input.data(), input.size(), 0);
					if (status != 0)
						throw std::runtime_error("Failed to hash data");
					status = BCryptFinishHash(hHash, hash.data(), hashLength, 0);
					if (status != 0)
						throw std::runtime_error("Failed to finish hash");
				}
				catch (std::runtime_error e)
				{
					BCryptDestroyHash(hHash);
					throw std::runtime_error(e);
				}
			}
			catch (std::runtime_error e)
			{
				BCryptCloseAlgorithmProvider(hAlgorithm, 0);
				throw std::runtime_error(e);
			}
			BCryptDestroyHash(hHash);
			BCryptCloseAlgorithmProvider(hAlgorithm, 0);

			return bytes_to_hex_string(hash);
		}

		void RecurseDirectoryPaths(std::vector<std::string>& paths, std::string directory, bool ignore_versioning)
		{
			std::vector<std::string> dirs = raidhook::Util::GetDirectoryContents(directory, true);
			std::vector<std::string> files = raidhook::Util::GetDirectoryContents(directory);
			for (auto it = files.begin(); it < files.end(); it++)
			{
				std::string fpath = directory + *it;

				// Add the path on the list
				paths.push_back(fpath);
			}
			for (auto it = dirs.begin(); it < dirs.end(); it++)
			{
				if (*it == "." || *it == "..")
					continue;
				if (ignore_versioning && (*it == ".hg" || *it == ".git"))
					continue;
				RecurseDirectoryPaths(paths, directory + *it + "/", false);
			}
		}

		static bool CompareStringsCaseInsensitive(std::string a, std::string b)
		{
			std::transform(a.begin(), a.end(), a.begin(), ::tolower);
			std::transform(b.begin(), b.end(), b.begin(), ::tolower);

			return a < b;
		}

		std::string GetDirectoryHash(std::string directory)
		{
			std::vector<std::string> paths;
			RecurseDirectoryPaths(paths, directory, true);

			// Case-insensitive sort, since that's how it was always done.
			// (on Windows, the filenames were previously downcased in RecurseDirectoryPaths, but that
			//  obviously won't work with a case-sensitive filesystem)
			// If I were to rewrite BLT from scratch I'd certainly make this case-sensitive, but there's no good
			//  way to change this without breaking hashing on previous versions.
			std::sort(paths.begin(), paths.end(), CompareStringsCaseInsensitive);

			std::string hashconcat;

			for (auto it = paths.begin(); it < paths.end(); it++)
			{
				std::string hashstr = sha256(raidhook::Util::GetFileContents(*it));
				hashconcat += hashstr;
			}

			return sha256(hashconcat);
		}

		std::string GetFileHash(std::string file)
		{
			// This has to be hashed twice otherwise it won't be the same hash if we're checking against a file uploaded
			// to the server
			std::string hash = sha256(raidhook::Util::GetFileContents(file));
			return sha256(hash);
		}

		template <> std::string ToHex(uint64_t value)
		{
			std::stringstream ss;
			ss << std::hex << std::setw(16) << std::setfill('0') << value;
			return ss.str();
		}

		std::string GetDllVersion()
		{
			std::string ret = "0.0.0.0";
			HMODULE hModule;
			GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCTSTR>(GetFileHash), &hModule);
			char path[MAX_PATH + 1];
			size_t pathSize = GetModuleFileName(hModule, path, sizeof(path) - 1);
			path[pathSize] = '\0';

			DWORD verHandle = 0;
			UINT size = 0;
			LPBYTE lpBuffer = NULL;
			uint32_t verSize = GetFileVersionInfoSize(path, &verHandle);

			if (verSize == 0)
			{
				return ret;
			}

			std::string verData;
			verData.resize(verSize);

			if (!GetFileVersionInfo(path, verHandle, verSize, verData.data()))
			{
				return ret;
			}

			if (!VerQueryValue(verData.data(), "\\", (VOID FAR * FAR*)&lpBuffer, &size))
			{
				return ret;
			}

			if (size == 0)
			{
				return ret;
			}

			VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
			if (verInfo->dwSignature != 0xfeef04bd)
			{
				return ret;
			}

			ret = std::format("{}.{}.{}.{}", (verInfo->dwFileVersionMS >> 16) & 0xFFFF,
			                  (verInfo->dwFileVersionMS >> 0) & 0xFFFF, (verInfo->dwFileVersionLS >> 16) & 0xFFFF,
			                  (verInfo->dwFileVersionLS >> 0) & 0xFFFF);
			return ret;
		}

		std::string GetModuleFileNameCxx(HMODULE hModule)
		{
			std::string buffer(MAX_PATH, '\0');
			DWORD len = GetModuleFileNameA(hModule, buffer.data(), buffer.size());
			// len is 0 for an error, which just makes us return an empty string.

			buffer.resize(len);
			return buffer;
		}

	} // namespace Util
} // namespace raidhook
