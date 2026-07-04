
#include "util/util.h"

#include <fstream>
#include <string>
#include <vector>

using namespace std;

namespace raidhook
{
	namespace Util
	{

		string GetFileContents(const string& filename)
		{
			ifstream t(filename, std::ifstream::binary);
			string str;

			t.seekg(0, std::ios::end);
			str.reserve(static_cast<string::size_type>(t.tellg()));
			t.seekg(0, std::ios::beg);
			str.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());

			return str;
		}

		// FIXME this function really should return a boolean, if it succeeded
		void EnsurePathWritable(const std::string& path)
		{
			int finalSlash = path.find_last_of('/');
			std::string finalPath = path.substr(0, finalSlash);
			if (DirectoryExists(finalPath))
				return;
			CreateDirectoryPath(finalPath);
		}

		bool RemoveFilesAndDirectory(const std::string& path)
		{
			std::vector<std::string> dirs = raidhook::Util::GetDirectoryContents(path, true);
			std::vector<std::string> files = raidhook::Util::GetDirectoryContents(path);
			bool failed = false;

			for (auto it = files.begin(); it < files.end(); it++)
			{
				failed = remove((path + "/" + *it).c_str());
				if (failed)
					return false;
			}
			for (auto it = dirs.begin(); it < dirs.end(); it++)
			{
				if (*it == "." || *it == "..")
					continue;

				// dont follow symlinks, just delete them as a file, recurse on normal directories
				if (raidhook::Util::IsSymlink(path + "/" + *it))
				{
					failed = remove((path + "/" + *it).c_str());
				}
				else
				{
					failed = raidhook::Util::RemoveFilesAndDirectory(path + "/" + *it) == 0;
				}
				if (failed)
					return false;
			}
			return RemoveEmptyDirectory(path);
		}

		bool CreateDirectoryPath(const std::string& path)
		{
			std::string newPath = "";
			std::vector<std::string> paths = Util::SplitString(path, '/');
			for (const auto& i : paths)
			{
				newPath = newPath + i + "/";
				CreateDirectorySingle(newPath);
			}
			return true;
		}

		void SplitString(const std::string& s, char delim, std::vector<std::string>& elems)
		{
			std::istringstream ss(s);
			std::string item;
			while (std::getline(ss, item, delim))
			{
				if (!item.empty())
				{
					elems.push_back(item);
				}
			}
		}

		std::vector<std::string> SplitString(const std::string& s, char delim)
		{
			std::vector<std::string> elems;
			SplitString(s, delim, elems);
			return elems;
		}

	} // namespace Util
} // namespace raidhook
