#include "ScriptData.h"

#include <cassert>
#include <functional>

// For the writer
#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>

// Testing
#include <chrono>

#include "util/util.h"

namespace raidhook::scriptdata
{
	using namespace tools;

	const SNil SNil::INSTANCE;
	const SBool SBool::STRUE(true);
	const SBool SBool::SFALSE(false);

	// static bool is32bit;

	template <typename Ptr> struct RawVec
	{
		unsigned int count;
		unsigned int capacity;
		Ptr offset;
		Ptr ignore; // Allocator
	};

	template <typename Ptr> struct RawStr
	{
		Ptr ignore; // Allocator
		Ptr str;
	};

	template <typename Ptr> struct RawTable
	{
		Ptr meta;
		RawVec<Ptr> contents;
	};

	typedef RawVec<uint32_t> RawVec32;
	typedef RawVec<uint64_t> RawVec64;
	static_assert(sizeof(RawVec32) == 16, "RawVec (32-bit) is the wrong size!");
	static_assert(sizeof(RawVec64) == 24, "RawVec (64-bit) is the wrong size!");

	typedef RawStr<uint32_t> RawStr32;
	typedef RawStr<uint64_t> RawStr64;
	static_assert(sizeof(RawStr32) == 8, "RawStr (32-bit) is the wrong size!");
	static_assert(sizeof(RawStr64) == 16, "RawStr (64-bit) is the wrong size!");

	typedef RawTable<uint32_t> RawTable32;
	typedef RawTable<uint64_t> RawTable64;
	static_assert(sizeof(RawTable32) == 20, "RawTable (32-bit) is the wrong size!");
	static_assert(sizeof(RawTable64) == 32, "RawTable (64-bit) is the wrong size!");

	typedef uint32_t valid_t;

	template <typename T> struct VecInfo
	{
		size_t count;
		T* items;
	};

	template <typename T> VecInfo<T> readVec(bool is32bit, const uint8_t* data, size_t& offset)
	{
		VecInfo<T> info = {};

		if (is32bit)
		{
			RawVec32& vec = *(RawVec32*)&data[offset];
			info.count = vec.count;
			info.items = (T*)&data[vec.offset];
			offset += sizeof(RawVec32);
		}
		else
		{
			RawVec64& vec = *(RawVec64*)&data[offset];
			info.count = vec.count;
			info.items = (T*)&data[vec.offset];
			offset += sizeof(RawVec64);
		}

		return info;
	}

	template <typename from, typename to> using Reader = std::function<void(const from&, to&)>;

	template <typename from, typename to>
	void readIntoVec(bool is32bit, std::vector<to>& res, const uint8_t* data, size_t& offset, Reader<from, to> f)
	{
		VecInfo<from> info = readVec<from>(is32bit, data, offset);

		res.clear();
		res.resize(info.count);

		for (size_t i = 0; i < info.count; i++)
		{
			f(info.items[i], res[i]);
		}
	}

	SItem::~SItem()
	{
	}

	template <typename T> void numberList(std::vector<T>& items)
	{
		for (size_t i = 0; i < items.size(); i++)
		{
			items[i].index = i;
		}
	}

	void ScriptData::ReadTable(STable& out, std::pair<valid_t, valid_t>* data, size_t count, uint32_t meta)
	{
		for (size_t i = 0; i < count; i++)
		{
			const SItem* key = Read(data[i].first);
			const SItem* val = Read(data[i].second);
			out.items[key] = val;
		}

		if (meta != ~0u)
		{
			out.meta = &strings[meta];
		}
		else
		{
			out.meta = nullptr;
		}
	}

	bool determine_is_32bit(size_t length, const uint8_t* data)
	{
		// The length of the 64-bit header
		// Any file shorter than this MUST be a 32-bit file
		const size_t header_len_64 = 8 + (6 * sizeof(RawVec64));

		if (length < header_len_64)
			return true;

		// See the format info file to see how this works

		// For the purposes of finding out the pointer width, treat the data
		//  as an integer array - the smaller units used in DslVector
		const uint32_t* ints = (const uint32_t*)data;

		// These macros check if the value at the given int position
#define CHECK_ZERO_32(index) \
	if (ints[index] != 0)    \
	return false
#define CHECK_ZERO_64(index) \
	if (ints[index] != 0)    \
	return true
#define CHECK_ZERO_64_PTR(index)                  \
	if (ints[index] != 0 || ints[index + 1] != 0) \
	return true

		// Check the allocator slots are empty - if so, it's certainly not 64-bit
		CHECK_ZERO_64_PTR(0);
		CHECK_ZERO_64_PTR(6);
		CHECK_ZERO_64_PTR(12);
		CHECK_ZERO_64_PTR(18);
		CHECK_ZERO_64_PTR(24);
		CHECK_ZERO_64_PTR(30);
		CHECK_ZERO_64_PTR(36);

#undef CHECK_ZERO_32
#undef CHECK_ZERO_64
#undef CHECK_ZERO_64_PTR

		// At this point, we can be pretty sure it's a 64-bit file
		return false;
	}

	ScriptData::ScriptData(size_t length, const uint8_t* data)
	{
		bool is32bit = determine_is_32bit(length, data);

		size_t offset = 0;

		// Skip the ScriptData's allocator
		offset += is32bit ? 4 : 8;

		static_assert(sizeof(float) == 4, "incompatible float size");
		readIntoVec<float, SNum>(is32bit, numbers, data, offset, [](const float& in, SNum& out) { out = SNum(in); });

		if (is32bit)
		{
			readIntoVec<RawStr32, SString>(is32bit, strings, data, offset, [data](const RawStr32& in, SString& out)
			                               { out = SString((const char*)&data[in.str]); });
		}
		else
		{
			readIntoVec<RawStr64, SString>(is32bit, strings, data, offset, [data](const RawStr64& in, SString& out)
			                               { out = SString((const char*)&data[in.str]); });
		}

		readIntoVec<float[3], SVector>(is32bit, vectors, data, offset,
		                               [](const float (&in)[3], SVector& out) { out = SVector(in[0], in[1], in[2]); });

		readIntoVec<float[4], SQuaternion>(is32bit, quats, data, offset, [](const float (&in)[4], SQuaternion& out)
		                                   { out = SQuaternion(in[0], in[1], in[2], in[3]); });

		readIntoVec<uint64_t, SIdstring>(is32bit, idstrings, data, offset,
		                                 [](const uint64_t& in, SIdstring& out) { out = SIdstring(in); });

		if (is32bit)
		{
			readIntoVec<RawTable32, STable>(is32bit, tables, data, offset,
			                                [data, this](const RawTable32& in, STable& out)
			                                {
												out = STable();
												ReadTable(out, (std::pair<valid_t, valid_t>*)&data[in.contents.offset],
				                                          in.contents.count, in.meta);
											});
		}
		else
		{
			readIntoVec<RawTable64, STable>(is32bit, tables, data, offset,
			                                [data, this](const RawTable64& in, STable& out)
			                                {
												out = STable();
												ReadTable(out, (std::pair<valid_t, valid_t>*)&data[in.contents.offset],
				                                          in.contents.count, in.meta);
											});
		}

		numberList(numbers);
		numberList(strings);
		numberList(vectors);
		numberList(quats);
		numberList(idstrings);
		numberList(tables);

		uint32_t* val = (uint32_t*)&data[offset];
		root = Read(*val);
	}

	const SItem* ScriptData::Read(uint32_t val)
	{
		uint8_t type = val >> 24;
		uint32_t index = val & 0xFFFFFF;

		switch (type)
		{
		case SNil::ID:
			return &SNil::INSTANCE;
		case SBool::ID_F:
			return &SBool::SFALSE;
		case SBool::ID_T:
			return &SBool::STRUE;
		case SNum::ID:
			return &numbers[index];
		case SString::ID:
			return &strings[index];
		case SVector::ID:
			return &vectors[index];
		case SQuaternion::ID:
			return &quats[index];
		case SIdstring::ID:
			return &idstrings[index];
		case STable::ID:
			return &tables[index];
		default:
			throw std::exception();
		}
	}

	// WRITING

	using std::ostream;

	class SItem::write_info
	{
	  public:
		uint32_t IndexOf(const SItem* item, bool* added = nullptr)
		{
			if (added)
				*added = false;

			std::map<const SItem*, int>& oftype_indexes = item_positions[item->GetId()];

			auto existing_idx = oftype_indexes.find(item);
			if (existing_idx != oftype_indexes.end())
			{
				return existing_idx->second;
			}

			std::vector<const SItem*>& oftype = items[item->GetId()];

			if (frozen)
			{
				throw "Cannot add item after freeze";
			}

			int idx = oftype.size();
			oftype_indexes[item] = idx;
			oftype.push_back(item);
			if (added)
				*added = true;
			return idx;
		}

		const std::vector<const SItem*>& ListOf(int id)
		{
			return items[id];
		}

		void freeze()
		{
			frozen = true;
			blocks.clear();
			linkages.clear();
		}

		inline bool is32bit()
		{
			return use32bit;
		}

		explicit write_info(bool use32bit) : use32bit(use32bit)
		{
		}
		write_info(write_info&) = delete;

		write_block& create_block()
		{
			if (blocks_applied)
			{
				throw "cannot create blocks after they are applied";
			}

			write_block* block = new write_block();

			blocks.push_back(std::unique_ptr<write_block>(block));

			return *blocks.back();
		}

		void create_linkage(write_block& block, linkage::on_address_set_t cb)
		{
			linkage lk = linkage(&block, cb);
			linkages.push_back(std::move(lk));
		}

		void apply_blocks(ostream& out)
		{
			blocks_applied = true;

			for (const std::unique_ptr<write_block>& block : blocks)
			{
				block->write_to(out);
			}

			// TODO linkages
			for (const linkage& ln : linkages)
			{
				ln.on_address_set(ln.block->offset);
			}
		}

		const std::map<int, std::vector<const SItem*>>& Items()
		{
			return items;
		}

	  private:
		std::map<int, std::vector<const SItem*>> items;
		std::map<int, std::map<const SItem*, int>> item_positions;
		std::vector<std::unique_ptr<write_block>> blocks;
		std::vector<linkage> linkages;

		bool frozen = false;
		bool blocks_applied = false;
		bool use32bit = false;
	};

	static void writeRef(write_block& out, SItem::write_info* info, const SItem* item)
	{
		switch (item->GetId())
		{
		case SNil::ID:
		case SBool::ID_F:
		case SBool::ID_T:
			writeVal<uint32_t>(out, item->GetId() << 24);
			return;
		}

		uint32_t index = info->IndexOf(item);
		uint32_t val = (index & 0xFFFFFF) | (item->GetId() << 24);
		writeVal<uint32_t>(out, val);
	}

	std::string SItem::Serialise(bool use32bit) const
	{
		auto serialise_vector = [](write_block& out, SItem::write_info& data, int id)
		{
			const std::vector<const SItem*>& items = data.ListOf(id);

			// contents vector
			uint32_t count = items.size(); // 0xEFBEADDE;
			writeVal<uint32_t>(out, count); // count
			writeVal<uint32_t>(out, count); // capacity

			bool is32 = data.is32bit();

			// Write the contents pointer, which gets overwritten with the address of the contents block
			uint32_t pos = out.tellp();
			write_block& contents = data.create_block();
			data.create_linkage(contents,
			                    [is32, &out, pos](uint32_t offset)
			                    {
									out.seek(pos);
									writePtr(out, is32, offset);
								});
			writePtr(out, is32, 0); // contents

			writePtr(out, is32,
			         0 /* 0xDEADBEEF */); // allocator (overwritten, value doesn't matter for RAID, tool thinks it's
			                              // 32-bit if this is zero, so write an easily identifiable value here)

			// Write out the contents
			for (const SItem* item : items)
			{
				item->Serialise(contents, data);
			}
		};

		// RAIDHOOK_LOG_LOG("S:0");
		auto timer = std::chrono::high_resolution_clock::now();
		auto printtime = [&timer](std::string name)
		{
			auto now = std::chrono::high_resolution_clock::now();
			long time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - timer).count();
			std::string str = name + " : " + std::to_string(time_ms);
			// RAIDHOOK_LOG_LOG(str.c_str());
			timer = now;
		};

		write_info data(use32bit);

		printtime("S:1");

		// Explore the dependency tree between objects, to make sure we've found everything
		Register(
			[&data](const SItem* item)
			{
				bool added;
				data.IndexOf(item, &added);
				return added;
			});

		printtime("S:2");

		// Freeze them while serialising
		data.freeze();

		// Make sure we create the first block after freezing, otherwise it would be removed and be invalid
		write_block& out = data.create_block();

		// Allocator pointer
		// Written over during loading, afaik we can put anything here
		writePtr(out, use32bit, 0);

		printtime("S:3");

		static_assert(sizeof(float) == 4, "incompatible float size");
		// Write float array
		serialise_vector(out, data, SNum::ID);

		// Write string array
		serialise_vector(out, data, SString::ID);

		// Write vector array
		serialise_vector(out, data, SVector::ID);

		// Write quaternion array
		serialise_vector(out, data, SQuaternion::ID);

		// Write idstring array
		serialise_vector(out, data, SIdstring::ID);

		// Write table array
		serialise_vector(out, data, STable::ID);

		// Write reference to initial item
		writeRef(out, &data, this);

		printtime("S:4");

		std::stringstream outstream;
		data.apply_blocks(outstream);

		printtime("S:5");

		return outstream.str();
	}

	void SNum::Serialise(write_block& out, write_info& info) const
	{
		writeVal<float>(out, val);
	}

	void SString::Serialise(write_block& out, write_info& info) const
	{
		bool is32 = info.is32bit();

		// The allocator
		// as above, we should avoid zero here (though it probably doesn't really matter)
		writePtr(out, is32, 0 /* 0xDEADBEEF */);

		write_block& blk = info.create_block();
		uint32_t pos = out.tellp();
		info.create_linkage(blk,
		                    [is32, &out, pos](uint32_t offset)
		                    {
								out.seek(pos);
								writePtr(out, is32, offset);
							});

		// Write zero for the string offset for now, we'll overwrite this with the block as per above
		writePtr(out, info.is32bit(), 0);

		blk.stream() << val << ((char)0);
	}

	void SVector::Serialise(write_block& out, write_info& info) const
	{
		writeVal<float>(out, x);
		writeVal<float>(out, y);
		writeVal<float>(out, z);
	}

	void SQuaternion::Serialise(write_block& out, write_info& info) const
	{
		writeVal<float>(out, x);
		writeVal<float>(out, y);
		writeVal<float>(out, z);
		writeVal<float>(out, w);
	}

	void SIdstring::Serialise(write_block& out, write_info& info) const
	{
		writeVal<uint64_t>(out, val);
	}

	void STable::Serialise(write_block& out, write_info& info) const
	{
		bool is32 = info.is32bit();

		// meta
		if (meta)
			writePtr(out, is32, info.IndexOf(meta));
		else
			writePtr(out, is32, 0xFFFFFFFF);

		// contents vector
		uint32_t count = items.size();
		writeVal<uint32_t>(out, count); // count
		writeVal<uint32_t>(out, count); // capacity

		// Write the contents pointer, which gets overwritten with the address of the contents block
		write_block& contents = info.create_block();
		uint32_t pos = out.tellp();
		info.create_linkage(contents,
		                    [is32, &out, pos](uint32_t offset)
		                    {
								out.seek(pos);
								writePtr(out, is32, offset);
							});
		writePtr(out, is32, 0); // contents

		writePtr(out, is32, 0 /*0xDEADBEEF*/); // allocator - see earlier uses for a comment of this

		// Write out the contents
		for (std::pair<const SItem*, const SItem*> pair : items)
		{
			writeRef(contents, &info, pair.first);
			writeRef(contents, &info, pair.second);
		}
	}

	void STable::Register(RegReceiver receiver) const
	{
		// Try to add ourselves, if we've already been added then bail to avoid infinite recursion if we contain
		// ourselves
		bool added = receiver(this);
		if (!added)
			return;

		if (meta)
			meta->Register(receiver);

		for (std::pair<const SItem*, const SItem*> pair : items)
		{
			pair.first->Register(receiver);
			pair.second->Register(receiver);
		}
	}
}; // namespace raidhook::scriptdata
