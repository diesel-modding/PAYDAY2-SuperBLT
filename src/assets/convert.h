//
// Created by Campbell on 14/07/2026.
//

#pragma once

#include <string>
#include <cstdint>
#include <vector>

#include "subhook.h"

class BLTAbstractDataStore;
class PDString;

std::vector<uint8_t> ConvertScriptData(std::vector<uint8_t>&& data, const std::string& path);

bool CheckWwiseSoundbankRequiresConversion(BLTAbstractDataStore* datastore);
std::vector<uint8_t> ConvertWwiseSoundbank(std::vector<uint8_t>&& data, const std::string& path);

std::vector<uint8_t> ConvertAnimation(std::vector<uint8_t>&& data, const std::string& path);

std::vector<uint8_t> ConvertFont(std::vector<uint8_t>&& data, const std::string& path);

std::vector<uint8_t> ConvertMassunit(std::vector<uint8_t>&& data, const std::string& path);
