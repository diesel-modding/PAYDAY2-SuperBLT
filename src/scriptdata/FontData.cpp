#include "FontData.h"

#include <assert.h>
#include <sstream>

using namespace raidhook::scriptdata::font;
using namespace raidhook::scriptdata::tools;

typedef uint32_t ptr_t;

struct data_t
{
	const uint8_t* data;
	size_t length;

	size_t offset;

	bool is32bit;
};

static_assert(sizeof(glyph) == 10, "Glyph is of the incorrect size");
static_assert(sizeof(kerning) == 12, "kerning is of the incorrect size");
static_assert(sizeof(char_def) == 8, "char_def is of the incorrect size");

template <typename T> static T read(data_t& data)
{
	assert(data.offset + sizeof(T) <= data.length);

	T val = *(T*)(data.data + data.offset);
	data.offset += sizeof(T);
	return val;
}

static ptr_t readPtr(data_t& data)
{
	if (data.is32bit)
		return read<uint32_t>(data);
	else
		return read<uint64_t>(data);
}

template <typename T> static void readVec(data_t& data, std::vector<T>& out)
{
	int size = read<uint32_t>(data);
	/* int capacity = */ read<uint32_t>(data);

	ptr_t contents_offset = readPtr(data);
	/* ptr_t allocator = */ readPtr(data);

	out.resize(size);

	const T* contents = (const T*)&data.data[contents_offset];
	std::copy(contents, contents + size, out.begin());
}

bool FontData::is32bit(const std::string& data)
{
	if (data.length() < 50)
	{
		// TODO throw error properly
		abort();
	}

	const uint32_t* ints = (const uint32_t*)data.c_str();

	// If the glyphs and codepoints match up in the correct locations, this is a 32-bit file
	return ints[0] == ints[5] && ints[1] == ints[6];
}

FontData::FontData(const std::string& data)
{
	data_t d = {};
	d.data = (const uint8_t*)data.c_str();
	d.length = data.length();
	d.offset = 0;

	d.is32bit = is32bit(data);

	readVec<glyph>(d, glyphs);
	readPtr(d); // TODO what is this for?
	readVec<char_def>(d, codepoints);
	readPtr(d); // TODO what is this for?
	readPtr(d); // TODO what is this for?
	readVec<kerning>(d, kernings);

	ukn_bool = read<uint8_t>(d);

	// Pad out to align the allocator
	d.offset += d.is32bit ? 3 : 7;

	readPtr(d); // An allocator, probably for the string
	name = (const char*)(d.data + readPtr(d)); // The name of the font
	// printf("%s\n", name.c_str());

	size = read<uint32_t>(d);
	texture_width = read<uint32_t>(d);
	texture_height = read<uint32_t>(d);
	ukn5 = read<uint32_t>(d);
	line_height = read<uint32_t>(d);

	read<uint32_t>(d); // not saved

	// for(const char_def &c : codepoints) {
	// 	printf("Char %c maps to %3d\n", c.codepoint, c.id);
	// }

	assert(d.offset == (d.is32bit ? 96 : 144));
}

template <typename T> static void write_to_block(write_block& blk, const T& item)
{
	blk.stream().write((const char*)&item, sizeof(item));
}

static uint32_t write_vec(write_block& blk, bool is32bit, uint32_t size)
{
	write_to_block<uint32_t>(blk, size); // size
	write_to_block<uint32_t>(blk, size); // capacity

	uint32_t addr = blk.tellp();
	writePtr(blk, is32bit, 0xDEC0EDFE); // to be replaced with glyphs_b

	writePtr(blk, is32bit, 0xEFBEADDE); // unused

	return addr;
}

std::string FontData::Export(bool is32bit)
{
	std::stringstream out;

	write_block glyphs_b;
	for (const glyph& g : glyphs)
	{
		write_to_block(glyphs_b, g);
	}

	write_block codepoints_b;
	for (const char_def& c : codepoints)
	{
		write_to_block(codepoints_b, c);
	}

	write_block kernings_b;
	for (const kerning& k : kernings)
	{
		write_to_block(kernings_b, k);
	}

	write_block name_b;
	name_b.stream() << name << ((char)0);

	// Write the main block
	write_block main_b;

	uint32_t glyphs_p = write_vec(main_b, is32bit, glyphs.size());
	writePtr(main_b, is32bit, 0xEFBEADDE); // unused
	uint32_t codepoints_p = write_vec(main_b, is32bit, codepoints.size());
	writePtr(main_b, is32bit, 0xEFBEADDE); // unused
	writePtr(main_b, is32bit, 0xEFBEADDE); // unused
	uint32_t kernings_p = write_vec(main_b, is32bit, kernings.size());

	write_to_block<uint8_t>(main_b, ukn_bool);

	// Pad out to align the allocator
	{
		uint64_t tmp_zero = 0;
		main_b.stream().write((const char*)&tmp_zero, is32bit ? 3 : 7);
	}

	writePtr(main_b, is32bit, 0xEFBEADDE); // An allocator, probably for the string

	uint32_t name_p = main_b.tellp();
	writePtr(main_b, is32bit, 0xDEC0EDFE); // to be replaced by the name string - DEADBEEF

	write_to_block<uint32_t>(main_b, size);
	write_to_block<uint32_t>(main_b, texture_width);
	write_to_block<uint32_t>(main_b, texture_height);
	write_to_block<uint32_t>(main_b, ukn5);
	write_to_block<uint32_t>(main_b, line_height);

	write_to_block<uint32_t>(main_b, 0xEFBEADDE); // unused

	// Check it's the correct length
	assert(main_b.tellp() == (is32bit ? 96 : 144));

	// Put everything into the stream
	main_b.write_to(out);
	glyphs_b.write_to(out);
	codepoints_b.write_to(out);
	kernings_b.write_to(out);
	name_b.write_to(out);

	// write out the actual offsets
	main_b.seek(glyphs_p);
	writePtr(main_b, is32bit, glyphs_b.offset);

	main_b.seek(codepoints_p);
	writePtr(main_b, is32bit, codepoints_b.offset);

	main_b.seek(kernings_p);
	writePtr(main_b, is32bit, kernings_b.offset);

	main_b.seek(name_p);
	writePtr(main_b, is32bit, name_b.offset);

	return out.str();
}
