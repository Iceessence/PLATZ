#include "TextureDecode.hpp"
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace platz {
namespace {
using Microsoft::WRL::ComPtr;

void ensure(HRESULT result, const char* message) {
    if (FAILED(result))
        throw std::runtime_error(message);
}

std::uint32_t bigEndian(std::span<const std::uint8_t> bytes) {
    std::uint32_t result = 0;
    for (auto byte : bytes.first(4))
        result = (result << 8) | byte;
    return result;
}

void requireTextureExtent(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > BaseColorTexture::maxSide || height > BaseColorTexture::maxSide)
        throw std::runtime_error("Embedded textures must fit within 1024 × 1024 pixels.");
}

std::uint32_t pngCrc(std::span<const std::uint8_t> bytes) {
    static constexpr auto table = [] {
        std::array<std::uint32_t, 256> entries{};
        for (std::uint32_t i = 0; i < entries.size(); ++i) {
            auto value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
            entries[i] = value;
        }
        return entries;
    }();
    std::uint32_t crc = 0xffffffffu;
    for (auto byte : bytes)
        crc = (crc >> 8) ^ table[(crc ^ byte) & 0xff];
    return ~crc;
}

void verifyPng(std::span<const std::uint8_t> source) {
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (source.size() < signature.size() || !std::equal(signature.begin(), signature.end(), source.begin()))
        throw std::runtime_error("Invalid PNG signature.");
    auto chunks = source.subspan(signature.size());
    if (chunks.size() < 25 || bigEndian(chunks) != 13 || bigEndian(chunks.subspan(4)) != 0x49484452)
        throw std::runtime_error("Invalid PNG image header.");
    requireTextureExtent(bigEndian(chunks.subspan(8)), bigEndian(chunks.subspan(12)));
    while (chunks.size() >= 12) {
        const auto length = bigEndian(chunks);
        if (length > chunks.size() - 12)
            break;
        const auto field = chunks.subspan(4, 4 + length);
        if (pngCrc(field) != bigEndian(chunks.subspan(8 + length)))
            throw std::runtime_error("Embedded PNG failed its chunk checksum.");
        if (bigEndian(field) == 0x49454e44 && length == 0)
            return; // IEND
        chunks = chunks.subspan(12 + length);
    }
    throw std::runtime_error("Incomplete embedded PNG.");
}

struct ComApartment {
    const HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComApartment() {
        if (status != RPC_E_CHANGED_MODE)
            ensure(status, "Could not initialize Windows image decoding.");
    }
    ~ComApartment() {
        if (SUCCEEDED(status))
            CoUninitialize();
    }
};

struct ImageReader {
    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    std::array<std::uint32_t, 2> size{};
    ImageReader(std::span<const std::uint8_t> source, TextureEncoding encoding) {
        if (source.empty() || source.size() > std::numeric_limits<DWORD>::max())
            throw std::runtime_error("Invalid embedded texture size.");
        if (encoding == TextureEncoding::Png)
            verifyPng(source);
        ensure(
            CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "Could not start Windows image decoding.");
        ensure(factory->CreateStream(&stream), "Could not create an image stream.");
        ensure(
            stream->InitializeFromMemory(const_cast<BYTE*>(source.data()), static_cast<DWORD>(source.size())),
            "Could not read the embedded texture.");
        const auto& codec = encoding == TextureEncoding::Png ? CLSID_WICPngDecoder2 : CLSID_WICJpegDecoder;
        ensure(CoCreateInstance(codec, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder)),
               "The Windows PNG/JPEG decoder is unavailable.");
        ensure(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand),
               "Malformed embedded PNG or JPEG.");
        ensure(decoder->GetFrame(0, &frame), "Embedded image has no readable frame.");
        ensure(frame->GetSize(&size[0], &size[1]), "Could not read image dimensions.");
        requireTextureExtent(size[0], size[1]);
    }
};
} // namespace

std::array<std::uint32_t, 2> inspectTexture(std::span<const std::uint8_t> source, TextureEncoding encoding) {
    return ImageReader(source, encoding).size;
}

void decodeTexture(std::span<const std::uint8_t> source, const BaseColorTexture& texture,
                   std::span<std::uint8_t> rgba) {
    ImageReader reader(source, texture.encoding);
    if (reader.size[0] != texture.width || reader.size[1] != texture.height ||
        rgba.size() != texture.rgbaSize())
        throw std::runtime_error("Embedded texture dimensions changed.");
    ComPtr<IWICFormatConverter> converter;
    ensure(reader.factory->CreateFormatConverter(&converter), "Could not convert the texture to RGBA.");
    ensure(converter->Initialize(reader.frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                 nullptr, 0.0, WICBitmapPaletteTypeCustom),
           "Unsupported PNG or JPEG pixel layout.");
    ensure(converter->CopyPixels(nullptr, texture.width * 4, static_cast<UINT>(rgba.size()), rgba.data()),
           "Could not decode embedded texture pixels.");
}
} // namespace platz
