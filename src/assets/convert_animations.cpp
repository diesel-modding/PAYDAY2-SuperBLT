//
// Created by HW12Dev on 14/07/2026
//

// Only handles decompressing animations at the moment, upgrader for 32bit to 64bit has yet to be written

#include "convert.h"

// Please forgive me, writing a zlib compression routine from scratch is so painful
#include "fileio/zlibcompression.h"

std::vector<uint8_t> ConvertAnimation(std::vector<uint8_t>&& data, const std::string& path)
{

	if (data[0] == 0x78)
	{
		// We are zlib compressed

		uint32_t uncompressed_size = *(uint32_t*)(data.data() + data.size() - 4);

		// Intentional copy
		std::vector<uint8_t> data_copy = data;

		data.clear();
		data.resize(uncompressed_size);

		compression::ZlibDecompression::DecompressBuffer((char*)data_copy.data(), data_copy.size() - 4, (char*)data.data(),
		                                                 uncompressed_size);

	}

	// TODO: 32bit -> 64bit goes here

    return std::move(data);
}