#include "AssetDigest.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <stdexcept>

namespace platz {
std::string assetDigest(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char,32> hash{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
                   static_cast<ULONG>(bytes.size()),hash.data(),static_cast<ULONG>(hash.size()))<0)
        throw std::runtime_error("Could not hash the asset source.");
    std::string out;
    out.reserve(64);
    for (auto byte:hash) {
        out.push_back("0123456789abcdef"[byte>>4]);
        out.push_back("0123456789abcdef"[byte&15]);
    }
    return out;
}
}
