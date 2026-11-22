#include "RasterImage.hpp"
#include "AssetDigest.hpp"
#include <algorithm>
#include <stdexcept>

namespace platz {
RasterImage::RasterImage(unsigned width,unsigned height,std::vector<std::uint8_t> pixels)
    : pixels_(std::move(pixels)),digest_(assetDigest(pixels_)) {
    sampling_.width=width;
    sampling_.height=height;
    sampling_.wrapU=sampling_.wrapV=TextureWrap::Clamp;
    sampling_.min=sampling_.mag=TextureFilter::Nearest;
}
Raster RasterImage::create(unsigned width,unsigned height,std::vector<std::uint8_t> pixels) {
    if (!width || !height || width>maxSide || height>maxSide ||
        pixels.size()!=size_t(width)*height*4)
        throw std::runtime_error("RGBA images must have 1–512 pixels per side and four bytes per pixel.");
    return Raster(new RasterImage(width,height,std::move(pixels)));
}
bool sameRaster(const Raster& a,const Raster& b) {
    return a==b || (a && b && a->sampling()==b->sampling() && a->digest()==b->digest());
}
const SlotImage* slotImage(std::span<const SlotImage> images,unsigned slot) {
    const auto it=std::lower_bound(images.begin(),images.end(),slot,
        [](const auto& image,unsigned index){return image.slot<index;});
    return it!=images.end() && it->slot==slot ? &*it : nullptr;
}
}
