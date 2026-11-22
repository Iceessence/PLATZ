#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace platz {
std::string assetDigest(std::span<const std::uint8_t> bytes);
}
