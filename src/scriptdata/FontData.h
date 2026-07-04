#pragma once

#include "FormatTools.h"
#include <stdint.h>

#include <string>
#include <vector>

namespace raidhook::scriptdata::font
{

#pragma pack(push, 1)
	struct glyph
	{
		// uint8_t zero_1;
		// char ukn[4];
		// uint16_t x;
		// uint16_t y;
		// uint8_t zero_2;

		// doesn't matter, this is consistent between 32- and 64-bit
		char ukn[10];
	};
#pragma pack(pop)

	struct kerning
	{
		uint32_t val1;
		uint32_t val2;
		char ukn[4];
	};

	struct char_def
	{
		uint32_t codepoint;
		uint32_t id;
	};

	class FontData
	{
	  public:
		static bool is32bit(const std::string& data);

		explicit FontData(const std::string& data);

		std::string Export(bool is32bit);

	  private:
		std::vector<glyph> glyphs;
		std::vector<char_def> codepoints;
		std::vector<kerning> kernings;

		uint8_t ukn_bool; // probably a bool, can have a value of 1 and is a single byte
		std::string name; // name of the font
		uint32_t size;
		uint32_t texture_width;
		uint32_t texture_height;
		uint32_t ukn5;
		uint32_t line_height;
	};
}; // namespace raidhook::scriptdata::font
