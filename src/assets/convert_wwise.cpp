//
// Created by Campbell on 14/07/2026.
//

#include "convert.h"

#include "dbutil/Datastore.h"
#include <DieselWwiseSoundbank/src/soundbank.h>

bool CheckWwiseSoundbankRequiresConversion(BLTAbstractDataStore* datastore)
{
	uint8_t version; // Actually an uint32_t, but the version will fit in a byte
	datastore->read(8, &version, 1);

	return (Wwise::BankVersion)version != Wwise::BankVersion::V2013;
}

std::vector<uint8_t> ConvertWwiseSoundbank(std::vector<uint8_t>&& data, const std::string& path)
{
	(void)path;

	Wwise::Soundbank bnk(data.data(), data.size());
	data.clear();
	bnk.Convert(Wwise::BankVersion::V2022, data);
	return std::move(data);
}
