#include "DB.h"

#include <util/util.h>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <vector>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <zlib.h>

using namespace blt::db;
using blt::idstring;

static_assert(sizeof(void*) == sizeof(intptr_t));
using pos_t = std::ios::pos_type;
using FileList = std::vector<DslFile>&;
using FileMap = std::map<std::pair<idstring, idstring>, DslFile*>&;

static uint64_t monotonicTimeMicros()
{
	// https://docs.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps
	LARGE_INTEGER StartingTime;
	LARGE_INTEGER Frequency;

	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);

	StartingTime.QuadPart *= 1000000;
	StartingTime.QuadPart /= Frequency.QuadPart;
	return StartingTime.QuadPart;
}

#pragma pack(1)

struct dsl_Vector
{
	uint64_t ElementCount;
	uint64_t ElementCapacity;
	uint64_t ElementOffset;
};

#pragma pack()

static void skipVector(std::istream& in)
{
	in.seekg(sizeof(dsl_Vector), std::ios::cur);
}

template <typename T> static std::vector<T> loadVector(uint8_t* in, int offset, dsl_Vector& vec)
{
	std::vector<T> data;
	data.resize(vec.ElementCount);

	const auto* ptr = in + vec.ElementOffset + offset;
	auto size = vec.ElementCount * sizeof(T);

	// Read the vector contents
	memcpy(data.data(), ptr, size);
	return data;
}

template <typename T> static std::vector<T> loadVector(uint8_t* in, int offset)
{
	dsl_Vector vec = *reinterpret_cast<dsl_Vector*>(in);
	return loadVector<T>(in, offset, vec);
}

template <typename T> static std::vector<T> loadVector(std::istream& in, int offset, dsl_Vector& vec)
{
	std::vector<T> data;
	data.resize(vec.ElementCount);

	// Read the vector contents
	pos_t pos = in.tellg();
	in.seekg(vec.ElementOffset + offset);
	in.read((char*)data.data(), vec.ElementCount * sizeof(T));
	in.seekg(pos);

	return data;
}

template <typename T> static std::vector<T> loadVector(std::istream& in, int offset)
{
	// Read the vector metadata
	dsl_Vector vec = {0};
	in.read((char*)&vec, sizeof(vec));

	return loadVector<T>(in, offset, vec);
}

static std::mutex db_setup_mutex;

DieselDB* DieselDB::Instance()
{
	// Make sure we can't go setting it up twice in parallel
	std::lock_guard guard(db_setup_mutex);

	static DieselDB instance;
	return &instance;
}

static void loadPackageHeader(DieselBundle* bundle, const std::string& headerPath, const std::string& dataPath,
                              FileList);
static void loadBundleHeader(DieselBundle* bundle, const std::string& headerPath, const std::string& dataPath,
                             FileList);
static DieselBundle* loadPackageBundle(const std::string& dataPath);
static DieselBundle* loadBundle(const std::string& dataPath);

////////////////////////
////// DSL FILE ////////
////////////////////////

std::vector<uint8_t> DslFile::ReadContents(std::istream& fi) const
{
	unsigned int realLength = length;
	if (!HasLength())
	{
		// This is an end-of-file asset, so it's length is it's start until the end of the file
		fi.seekg(0, std::ios::end);
		realLength = (unsigned int)fi.tellg() - offset;
	}

	std::vector<uint8_t> result;

	if (bundle->ChunkOffsets.empty())
	{
		fi.seekg(offset, std::ios::beg);
		std::vector<uint8_t> data(length);
		fi.read((char*)data.data(), data.size());

		uint32_t dstSize = *reinterpret_cast<uint32_t*>(data.data() + (data.size() - sizeof(uint32_t)));
		result.resize(dstSize);

		auto destSize = static_cast<uLongf>(dstSize);
		auto sourceSize = static_cast<uLongf>(data.size());

		auto ret = uncompress2(result.data(), &destSize, data.data(), &sourceSize);

		if (ret != Z_OK)
			throw std::runtime_error("Failed to decompress data");
	}
	else
	{
		result.resize(realLength);

		size_t blockIdx = offset / 0x10000;
		size_t bufferOffset = offset % 0x10000;
		size_t destFileOffset = 0;

		std::vector<uint8_t> data(0x10000);

		while (destFileOffset < realLength)
		{
			uint32_t compressedLength;

			if (blockIdx >= bundle->ChunkOffsets.size())
				throw std::runtime_error("block index bigger then chunk offsets");

			auto fileOffset = bundle->ChunkOffsets[blockIdx];

			fi.seekg(fileOffset, std::ios::beg);
			fi.read((char*)&compressedLength, sizeof(compressedLength));

			std::vector<uint8_t> compressedData(compressedLength);
			fi.read((char*)compressedData.data(), compressedData.size());

			uint32_t dstSize =
				*reinterpret_cast<uint32_t*>(compressedData.data() + (compressedData.size() - sizeof(uint32_t)));
			data.resize(dstSize);

			auto destSize = static_cast<uLongf>(dstSize);
			auto sourceSize = static_cast<uLongf>(compressedData.size());

			auto ret = uncompress2(data.data(), &destSize, compressedData.data(), &sourceSize);

			if (ret != Z_OK)
				throw std::runtime_error("Failed to decompress data");

			auto dataPtr = data.data();

			if (destFileOffset == 0)
			{
				dataPtr += bufferOffset;
				destSize -= bufferOffset;
			}

			memcpy(result.data() + destFileOffset, dataPtr, min(destSize, (realLength - destFileOffset)));
			destFileOffset += destSize;

			++blockIdx;
		}
	}

	return result;
}

////////////////////////
////// DIESEL DB ///////
////////////////////////

DieselDB::DieselDB()
{
	uint64_t start_time = monotonicTimeMicros();
	RAIDHOOK_LOG_LOG("Start loading DB info");

	std::ifstream in;
	in.exceptions(std::ios::failbit | std::ios::badbit);

	// "Future" proofing.
	std::string blb_names[2] = {"all", "bundle_db"};

	std::string blb_suffix = ".blb";
	std::string blb_path;

	for (const std::string& name : raidhook::Util::GetDirectoryContents("assets"))
	{
		if (name.length() <= blb_suffix.size())
			continue;
		if (name.compare(name.size() - blb_suffix.size(), blb_suffix.size(), blb_suffix) != 0)
			continue;

		bool valid_name = false;
		for (const std::string& blb_name : blb_names)
		{
			if (name.compare(0, blb_name.size(), blb_name) == 0)
			{
				valid_name = true;
				break;
			}
		}

		if (!valid_name)
			continue;

		blb_path = name;
	}

	if (blb_path.empty())
	{
		RAIDHOOK_LOG_ERROR("No 'all.blb' or 'bundle_db.blb' found in 'assets' folder, not loading asset database!");
		return;
	}

	in.open("assets/" + blb_path, std::ios::binary);

	in.seekg(sizeof(uint32_t) * 2, std::ios::beg);

	// Build out the LanguageID-to-idstring mappings
	struct LanguageData
	{
		idstring name;
		uint32_t id;
		uint32_t padding; // Probably padding, at least - always zero
	};
	static_assert(sizeof(LanguageData) == 16);
	std::map<int, idstring> languages;
	for (const LanguageData& lang : loadVector<LanguageData>(in, 0))
	{
		languages[lang.id] = lang.name;
	}

	// Sortmap
	in.seekg(sizeof(void*) * 2, std::ios::cur);
	in.seekg(sizeof(void*), std::ios::cur);

	// Files
	struct MiniFile
	{
		idstring type;
		idstring name;
		uint32_t langId;
		uint32_t zero_1;
		uint32_t fileId;
		uint32_t zero_2;
	};
	static_assert(sizeof(MiniFile) == 32); // Same on 32 and 64 bit
	std::vector<MiniFile> miniFiles = loadVector<MiniFile>(in, 0);
	filesList.resize(miniFiles.size());

	for (size_t i = 0; i < miniFiles.size(); i++)
	{
		MiniFile& mini = miniFiles[i];
		// printf("File: %016llx.%016llx\n", mini.name, mini.type);

		assert(mini.zero_1 == 0);
		assert(mini.zero_2 == 0);

		// Since the file IDs form a sequence of 1 upto the file count (though not in
		// order), we can use those as indexes into our file list.

		if (mini.fileId > filesList.size())
		{
			filesList.resize(mini.fileId);
		}

		DslFile& fi = filesList.at(mini.fileId - 1);

		fi.name = mini.name;
		fi.type = mini.type;
		fi.fileId = mini.fileId;

		// Look up the language idstring, if applicable
		fi.rawLangId = mini.langId;
		if (mini.langId == 0)
			fi.langId = 0;
		else if (languages.count(mini.langId))
			fi.langId = languages[mini.langId];
		else
			fi.langId = 0x11df684c9591b7e0; // 'unknown' - is in the hashlist, so you'll be able to find it

		// If it's a repeated file, the language must be different
		const auto& prev = files.find(fi.Key());
		if (prev != files.end())
		{
			assert(prev->second->langId != fi.langId);
			fi.next = prev->second;
		}

		files[fi.Key()] = &fi;
	}

	// printf("File count: %ld\n", files.size());

	// Load each of the bundle headers
	std::string suffix = "_h.bundle";
	std::string prefix = "all_";
	std::string stream_prefix = "stream_";
	for (const std::string& name : raidhook::Util::GetDirectoryContents("assets"))
	{
		if (name.length() <= suffix.size())
			continue;
		if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
			continue;
		if (name == "all_h.bundle")
			continue; // all_h handling later

		bool package = true;
		if (name.compare(0, prefix.size(), prefix) == 0)
			package = false;

		std::string headerPath = "assets/" + name;

		// Find the headerPath to the data file - chop out the '_h' bit
		std::string dataPath = headerPath;
		dataPath.erase(dataPath.end() - 9, dataPath.end() - 7);

		if (name.compare(0, stream_prefix.size(), stream_prefix) == 0)
			package = false;

		if (package)
		{
			DieselBundle* bundle = loadPackageBundle(dataPath);
			loadPackageHeader(bundle, headerPath, dataPath, filesList);
		}
		else
		{
			DieselBundle* bundle = loadBundle(dataPath);
			loadBundleHeader(bundle, headerPath, dataPath, filesList);
		}
	}

	// We're done loading, print out how long it took and how many files it's tracking (to estimate memory usage)
	uint64_t end_time = monotonicTimeMicros();

	char buff[1024];
	memset(buff, 0, sizeof(buff));
	snprintf(buff, sizeof(buff) - 1, "Finished loading DB info: %zd files in %d ms", filesList.size(),
	         (int)(end_time - start_time) / 1000);
	RAIDHOOK_LOG_LOG(buff);
}

static void loadPackageHeader(DieselBundle* bundle, const std::string& headerPath, const std::string& dataPath,
                              FileList files)
{
	bundle->headerPath = headerPath;
	bundle->path = dataPath;

	std::ifstream in;
	in.exceptions(std::ios::failbit | std::ios::badbit);
	in.open(bundle->headerPath, std::ios::binary);

	in.seekg(sizeof(uint64_t), std::ios::beg);

	uint32_t compressedSize;
	in.read((char*)&compressedSize, sizeof(compressedSize));

	std::vector<uint8_t> srcData(compressedSize);
	in.read((char*)srcData.data(), srcData.size());

	uint32_t dstSize = *reinterpret_cast<uint32_t*>(srcData.data() + (srcData.size() - sizeof(uint32_t)));

	std::vector<uint8_t> dstData(dstSize);

	auto destSize = static_cast<uLongf>(dstData.size());
	auto sourceSize = static_cast<uLongf>(srcData.size());

	auto ret = uncompress2(dstData.data(), &destSize, srcData.data(), &sourceSize);

	if (ret != Z_OK)
		throw std::runtime_error("Failed to decompress package header");

	// if BlockSize != 0 then there are more vector blocks in this file
	uint32_t blockSize = *reinterpret_cast<uint32_t*>(dstData.data());

	// Files
	struct FilePos
	{
		int32_t fileId;
		int32_t offset;
	};
	static_assert(sizeof(FilePos) == 8); // Same on 32 and 64 bit
	std::vector<FilePos> positions = loadVector<FilePos>(dstData.data() + sizeof(uint32_t), 0);

	for (size_t i = 0; i < positions.size(); ++i)
	{
		const auto& file = positions[i];
		DslFile* curr = &files.at(file.fileId - 1);

		curr->bundle = bundle;
		curr->offset = file.offset;

		if (i + 1 < positions.size())
		{
			const auto& nextFile = positions[i + 1];

			curr->length = nextFile.offset - file.offset;

			if (i > 0)
			{
				const auto& prevFile = files[i - 1];
				DslFile* prev = &files.at(prevFile.fileId - 1);

				if (file.offset == prevFile.offset)
					prev->length = curr->length;
			}
		}
	}

	if (positions.size() == 0)
		return;

	const auto& lastFile = positions[positions.size() - 1];
	DslFile* last = &files.at(lastFile.fileId - 1);

	last->length = bundle->DecompressedFileSize - lastFile.offset;

	if (positions.size() > 1)
	{
		const auto& secondLastFile = positions[positions.size() - 2];

		DslFile* secondLast = &files.at(secondLastFile.fileId - 1);

		if (secondLastFile.offset == lastFile.offset)
			secondLast->length = last->length;
	}
}

static void loadBundleHeader(DieselBundle* dieselBundle, const std::string& headerPath, const std::string& dataPath,
                             FileList files)
{
	std::ifstream in;
	in.exceptions(std::ios::failbit | std::ios::badbit);
	in.open(headerPath, std::ios::binary);

	in.seekg(sizeof(uint64_t), std::ios::beg);

	uint32_t compressedSize;
	in.read((char*)&compressedSize, sizeof(compressedSize));

	std::vector<uint8_t> srcData(compressedSize);
	in.read((char*)srcData.data(), srcData.size());

	uint32_t dstSize = *reinterpret_cast<uint32_t*>(srcData.data() + (srcData.size() - sizeof(uint32_t)));

	std::vector<uint8_t> dstData(dstSize);

	auto destSize = static_cast<uLongf>(dstData.size());
	auto sourceSize = static_cast<uLongf>(srcData.size());

	auto ret = uncompress2(dstData.data(), &destSize, srcData.data(), &sourceSize);

	if (ret != Z_OK)
		throw std::runtime_error("Failed to decompress package header");

	struct BundleInfo
	{
		intptr_t id;
		dsl_Vector vec;
		intptr_t zero;
		intptr_t one;
	};

	static_assert(sizeof(BundleInfo) == 48);

	struct ItemInfo
	{
		uint32_t fileId;
		uint32_t offset;
		uint32_t length;
	};
	static_assert(sizeof(ItemInfo) == 12); // True on 32/64 bit

	BundleInfo bundle = *reinterpret_cast<BundleInfo*>(dstData.data() + sizeof(uint32_t));
	size_t offset = sizeof(uint32_t) + sizeof(BundleInfo);

	assert(bundle.zero == 0);
	assert(bundle.one == 1);

	dieselBundle->headerPath = headerPath;
	dieselBundle->path = dataPath;

	for (ItemInfo item : loadVector<ItemInfo>(dstData.data() + sizeof(uint32_t), 0, bundle.vec))
	{
		DslFile* fi = &files.at(item.fileId - 1);
		fi->bundle = dieselBundle;
		fi->offset = item.offset; // Compressed File Offset
		fi->length = item.length;
	}
}

static DieselBundle* loadPackageBundle(const std::string& dataPath)
{
	// Memory leak, not an issue since it's a small amount and the DB doesn't get unloaded anyway
	DieselBundle* bundle = new DieselBundle();

	std::ifstream in;
	in.exceptions(std::ios::failbit | std::ios::badbit);
	in.open(dataPath, std::ios::binary | std::ios::ate);

	auto fileSize = in.tellg();
	in.seekg(0, std::ios::beg);

	uint64_t decompressedFileSize;
	in.read((char*)&decompressedFileSize, sizeof(decompressedFileSize));

	bundle->DecompressedFileSize = decompressedFileSize;

	while (in.tellg() < fileSize)
	{
		bundle->ChunkOffsets.push_back(in.tellg());

		uint32_t compressedSize;
		in.read((char*)&compressedSize, sizeof(compressedSize));

		in.seekg(compressedSize, std::ios::cur);
	}

	return bundle;
}

static DieselBundle* loadBundle(const std::string& dataPath)
{
	DieselBundle* bundle = new DieselBundle();

	bundle->DecompressedFileSize = ~0;

	return bundle;
}

DslFile* DieselDB::Find(idstring name, idstring ext)
{
	auto res = files.find(std::pair<idstring, idstring>(name, ext));

	// Not found?
	if (res == files.end())
	{
		return nullptr;
	}

	return res->second;
}

BLTAbstractDataStore* DieselDB::Open(DieselBundle* bundle)
{
	// Ideally we'd cache these to avoid opening files all the time, but this is
	// pretty much what the Linux version of PD2 seems to do internally. The
	// problem here is that the data store is reference counted by dsl::Archive, so
	// caching it will be somewhat difficult.
	// TODO: there is no raid linux binary tho, what now?
	BLTFileDataStore* fds = BLTFileDataStore::Open(bundle->path);
	return fds;
}
