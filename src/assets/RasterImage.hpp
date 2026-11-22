#pragma once
#include "TextureDecode.hpp"
#include <memory>
#include <string>
#include <vector>

namespace platz {
class RasterImage {
public:
    static constexpr unsigned maxSide=512;
    static constexpr unsigned maxImages=32;
    static constexpr size_t maxLibraryBytes=8u<<20;
    static std::shared_ptr<const RasterImage> create(unsigned width,unsigned height,std::vector<std::uint8_t> pixels);
    unsigned width() const { return sampling_.width; }
    unsigned height() const { return sampling_.height; }
    const std::vector<std::uint8_t>& pixels() const { return pixels_; }
    const std::string& digest() const { return digest_; }
    const BaseColorTexture& sampling() const { return sampling_; }
private:
    RasterImage(unsigned width,unsigned height,std::vector<std::uint8_t> pixels);
    BaseColorTexture sampling_;
    std::vector<std::uint8_t> pixels_;
    std::string digest_;
};
using Raster=std::shared_ptr<const RasterImage>;
bool sameRaster(const Raster& a,const Raster& b);

struct SlotImage {
    unsigned slot{};
    std::string imageId;
    Raster raster;
    bool operator==(const SlotImage& other) const {
        return slot==other.slot && imageId==other.imageId && sameRaster(raster,other.raster);
    }
};
const SlotImage* slotImage(std::span<const SlotImage> images,unsigned slot);
}
