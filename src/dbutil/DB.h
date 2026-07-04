#pragma once

#include "Datastore.h"
#include "platform.h"

#include <istream>
#include <map>
#include <vector>

namespace blt::db
{
	struct DieselBundle
	{
	  public:
		std::string path;
		std::string headerPath;
		size_t DecompressedFileSize;
		std::vector<size_t> ChunkOffsets;
	};

	struct DslFile
	{
	  public:
		idstring name;
		idstring type;
		int fileId;
		int rawLangId;
		idstring langId;

		/**
		 * If there are multiple of this kind of asset, but in different languages, then this
		 * points to another file with the same name/type but a different language.
		 */
		DslFile* next = nullptr;

		// These are used for reading, and are picked up from the bundle headers
		DieselBundle* bundle = nullptr;
		unsigned int offset = ~0u;
		unsigned int length = ~0u;

		[[nodiscard]] bool Found() const
		{
			return bundle != nullptr;
		}

		[[nodiscard]] bool HasLength() const
		{
			return length != ~0u;
		}

		[[nodiscard]] std::pair<idstring, idstring> Key() const
		{
			return std::pair<idstring, idstring>(name, type);
		}

		[[nodiscard]] std::vector<uint8_t> ReadContents(std::istream& fi) const;
	};

	class DieselDB
	{
	  private:
		DieselDB();

	  public:
		DieselDB(const DieselDB&) = delete;
		DieselDB& operator=(const DieselDB&) = delete;

		DslFile* Find(idstring name, idstring ext);

		static DieselDB* Instance();

		BLTAbstractDataStore* Open(DieselBundle* bundle);

	  private:
		std::vector<DslFile> filesList;
		std::map<std::pair<idstring, idstring>, DslFile*> files;
	};

}; // namespace blt::db
