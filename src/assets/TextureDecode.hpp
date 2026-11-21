#pragma once
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace platz {
enum class TextureEncoding { Png, Jpeg };
enum class TextureWrap { Repeat, Clamp, Mirror };
enum class TextureFilter { Linear, Nearest };
struct BaseColorTexture {
    static constexpr std::uint32_t maxSide = 1024;
    static constexpr std::uint32_t maxActive = 64;
    static constexpr std::size_t maxActiveBytes = 64u << 20;
    std::size_t offset{}, length{};
    std::uint32_t width{}, height{};
    TextureEncoding encoding{};
    TextureWrap wrapU{}, wrapV{};
    TextureFilter min{}, mag{};
    auto operator<=>(const BaseColorTexture&) const = default;
    std::size_t rgbaSize() const { return std::size_t(width) * height * 4; }
};

std::array<std::uint32_t, 2> inspectTexture(std::span<const std::uint8_t> source, TextureEncoding encoding);
void decodeTexture(std::span<const std::uint8_t> source, const BaseColorTexture& texture,
                   std::span<std::uint8_t> rgba);
} // namespace platz
