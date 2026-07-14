//
// Created by Campbell on 14/07/2026.
//

#pragma once

#include <string>
#include <vector>

class BLTAbstractDataStore;

std::vector<uint8_t> ConvertScriptData(std::vector<uint8_t>&& data, const std::string& path);

bool CheckWwiseSoundbankRequiresConversion(BLTAbstractDataStore* datastore);
std::vector<uint8_t> ConvertWwiseSoundbank(std::vector<uint8_t>&& data, const std::string& path);
