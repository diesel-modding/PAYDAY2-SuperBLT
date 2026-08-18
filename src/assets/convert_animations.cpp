//
// Created by HW12Dev on 14/07/2026
//

#include "convert.h"

// Please forgive me, writing a zlib compression routine from scratch is so painful
#include "fileio/zlibcompression.h"
#include "util/util.h"

#include <diesel/animation.h>

struct AnimationHeader // 32bit, 64bit has extra padding here on purpose
{
	uint32_t type_id;
	uint32_t version;
	uint32_t original_location;
	uint32_t file_size;
};

std::vector<uint8_t> ConvertAnimation(std::vector<uint8_t>&& data, const std::string& path)
{
	if (data.size() < sizeof(AnimationHeader))
		return data;

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

	AnimationHeader* header = (AnimationHeader*)data.data();

	if ((size_t)header->file_size != data.size()) // size field doesn't align up to have the right data, must be 64bit
	{
		return data;
	}
	
	// Parse the contents in 32-bit format
	diesel::Animation animation;
	Reader reader((char*)data.data(), data.size(), false);

	if (!animation.ReadUncompressed(reader, diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::PAYDAY_2_LATEST,
	                                                                   diesel::Renderer::UNSPECIFIED,
	                                                                   diesel::FileSourcePlatform::WINDOWS_32)))
	{
		char msg[512];
		snprintf(msg, sizeof(msg), "Error occurred while reading 32bit Animation, is the file corrupt? File: %s",
		         path.c_str());
		RAIDHOOK_LOG_LOG(msg);

		return data;
	}

	reader.Close();

	// Now write it back out to our data vector

	Writer writer;
	MemoryWriterContainer* container = (MemoryWriterContainer*)writer.GetContainer();

	animation.Write(writer,
	         diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::DIESEL_V3, diesel::Renderer::UNSPECIFIED,
	                                                diesel::FileSourcePlatform::WINDOWS_64));

	writer.Close();

	// Nasty bodge, I'm sure this is undefined behaviour but it will work here :)
	std::vector<char> signedData = container->TakeData();
	std::vector<uint8_t>* aliasingViolationLivesHere = (std::vector<uint8_t>*)&signedData;
	std::vector<uint8_t> unsignedData = std::move(*aliasingViolationLivesHere);

	return unsignedData;
}