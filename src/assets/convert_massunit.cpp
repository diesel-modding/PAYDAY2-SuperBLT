//
// Created by HW12Dev on 30/07/2026
//

#include "convert.h"

#include "util/util.h"

#include <diesel/modern/massunit.h>

struct MassunitHeader
{
	uint32_t types_size;
	uint32_t types_capacity;
	uint32_t types_data;
	uint32_t types_allocator;
};

struct MassunitHeader64bit
{
	uint64_t types_size;
	uint64_t types_capacity;
	uint64_t types_data;
	uint64_t types_allocator;
};

std::vector<uint8_t> ConvertMassunit(std::vector<uint8_t>&& data, const std::string& path)
{
	// Empty massunits that were created by the old DieselEngineFormats can be smaller than 64 bit header
	if (data.size() < sizeof(MassunitHeader))
		return data;

	MassunitHeader64bit* header = (MassunitHeader64bit*)data.data();

	if (header->types_allocator == 0)
	{
		return data; // is already 64bit
	}

	// Parse the contents in 32-bit format
	diesel::modern::MassUnitResource mu;
	Reader reader((char*)data.data(), data.size(), false);

	if (!mu.Read(reader, diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::PAYDAY_2_LATEST,
	                                                            diesel::Renderer::UNSPECIFIED,
	                                                            diesel::FileSourcePlatform::WINDOWS_32)))
	{
		char msg[512];
		snprintf(msg, sizeof(msg), "Error occurred while reading 32bit Massunit, is the file corrupt? File: %s",
		         path.c_str());
		RAIDHOOK_LOG_LOG(msg);

		return data;
	}

	reader.Close();

	// Now write it back out to our data vector

	Writer writer;
	MemoryWriterContainer* container = (MemoryWriterContainer*)writer.GetContainer();

	mu.Write(writer,
	         diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::DIESEL_V3, diesel::Renderer::UNSPECIFIED,
	                                                diesel::FileSourcePlatform::WINDOWS_64));

	writer.Close();

	// Nasty bodge, I'm sure this is undefined behaviour but it will work here :)
	std::vector<char> signedData = container->TakeData();
	std::vector<uint8_t>* aliasingViolationLivesHere = (std::vector<uint8_t>*)&signedData;
	std::vector<uint8_t> unsignedData = std::move(*aliasingViolationLivesHere);

	return unsignedData;
}
