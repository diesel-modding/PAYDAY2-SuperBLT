#include "util.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <openssl/evp.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pd2hook
{
	namespace Util
	{

		Exception::Exception(const char *file, int line) :
			mFile(file), mLine(line)
		{}

		Exception::Exception(std::string msg, const char *file, int line) :
			mFile(file), mLine(line), mMsg(std::move(msg))
		{}

		const char *Exception::what() const throw()
		{
			if (!mMsg.empty())
			{
				return mMsg.c_str();
			}

			return std::exception::what();
		}

		const char *Exception::exceptionName() const
		{
			return "An exception";
		}

		void Exception::writeToStream(std::ostream& os) const
		{
			os << exceptionName() << " occurred @ (" << mFile << ':' << mLine << "). " << what();
		}

		// from https://stackoverflow.com/a/62605880/11871110
		// Updated sha256 functions to use latest OpenSSL/LibreSSL
		
		//helper function to print the digest bytes as a hex string
		std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes)
		{
			std::ostringstream stream;
			for (uint8_t b : bytes)
			{
				stream << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(b);
			}
			return stream.str();
		}

		//perform the SHA3-512 hash
		std::string sha256(const std::string& input)
		{
			uint32_t digest_length = 32;
			const EVP_MD* algorithm = EVP_sha256();
			uint8_t* digest = static_cast<uint8_t*>(OPENSSL_malloc(digest_length));
			EVP_MD_CTX* context = EVP_MD_CTX_new();
			EVP_DigestInit_ex(context, algorithm, nullptr);
			EVP_DigestUpdate(context, input.c_str(), input.size());
			EVP_DigestFinal_ex(context, digest, &digest_length);
			EVP_MD_CTX_destroy(context);
			std::string output = bytes_to_hex_string(std::vector<uint8_t>(digest, digest + digest_length));
			OPENSSL_free(digest);
			return output;
		}

		void RecurseDirectoryPaths(std::vector<std::string>& paths, std::string directory, bool ignore_versioning)
		{
			std::vector<std::string> dirs = pd2hook::Util::GetDirectoryContents(directory, true);
			std::vector<std::string> files = pd2hook::Util::GetDirectoryContents(directory);
			for (auto it = files.begin(); it < files.end(); it++)
			{
				std::string fpath = directory + *it;

				// Add the path on the list
				paths.push_back(fpath);
			}
			for (auto it = dirs.begin(); it < dirs.end(); it++)
			{
				if (*it == "." || *it == "..") continue;
				if (ignore_versioning && (*it == ".hg" || *it == ".git")) continue;
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
				std::string hashstr = sha256(pd2hook::Util::GetFileContents(*it));
				hashconcat += hashstr;
			}

			return sha256(hashconcat);
		}

		std::string GetFileHash(std::string file)
		{
			// This has to be hashed twice otherwise it won't be the same hash if we're checking against a file uploaded to the server
			std::string hash = sha256(pd2hook::Util::GetFileContents(file));
			return sha256(hash);
		}

		template<>
		std::string ToHex(uint64_t value)
		{
			std::stringstream ss;
			ss << std::hex << std::setw(16) << std::setfill('0') << value;
			return ss.str();
		}

		bool IsVr()
		{
			bool is_vr = false;

#ifdef _WIN32
			TCHAR processPath[MAX_PATH + 1];
			GetModuleFileName(NULL, processPath, MAX_PATH + 1);
			std::string processPathString = processPath;
			is_vr = processPathString.rfind("_vr.exe") == processPathString.length() - 7;
#endif

			return is_vr;
		}
	}
}
