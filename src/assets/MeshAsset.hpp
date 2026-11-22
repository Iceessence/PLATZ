#pragma once
#include "TextureDecode.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace platz {
struct MeshVertex {
    std::array<float, 3> position, normal;
    std::array<float, 2> uv{};
};
struct MeshPrimitive {
    std::uint32_t firstIndex, indexCount, slot;
    bool hasUv{};
};
struct MeshMaterialSlot {
    std::string name;
    std::optional<std::array<float, 3>> sourceColor;
    std::optional<BaseColorTexture> texture;
    std::string textureNote;
    bool hasSourceTexture() const { return texture || !textureNote.empty(); }
};
struct SlotColor {
    std::uint32_t slot;
    std::array<float, 3> srgb;
    bool operator==(const SlotColor&) const = default;
};

// Immutable source bytes and their rebuildable geometry travel together in memory.
class MeshAsset {
  public:
    static constexpr std::size_t maxBytes = 2u << 20;
    static constexpr std::uint32_t maxPrimitives = 32;
    static std::shared_ptr<const MeshAsset> load(const std::filesystem::path&);
    static std::shared_ptr<const MeshAsset> decode(std::span<const std::uint8_t>);
    const std::string& digest() const { return digest_; }
    const auto& source() const { return source_; }
    const auto& vertices() const { return vertices_; }
    const auto& indices() const { return indices_; }
    const auto& primitives() const { return primitives_; }
    const auto& materialSlots() const { return slots_; }
    bool supportsImageSlot(unsigned slot) const;
    const BaseColorTexture* textureForSlot(std::uint32_t slot, std::span<const std::uint32_t> disabled) const;
    void readTexturePixels(const BaseColorTexture& texture, std::span<std::uint8_t> rgba) const;
    std::array<float, 3> colorForSlot(std::uint32_t slot, std::array<float, 3> fallback,
                                      std::span<const SlotColor> overrides = {}) const {
        for (const auto& entry : overrides)
            if (entry.slot == slot)
                return entry.srgb;
        return slots_.at(slot).sourceColor.value_or(fallback);
    }
    const auto& minCorner() const { return min_; }
    const auto& maxCorner() const { return max_; }

  private:
    MeshAsset() = default;
    std::string digest_;
    std::vector<std::uint8_t> source_;
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<MeshPrimitive> primitives_;
    std::vector<MeshMaterialSlot> slots_;
    std::array<float, 3> min_{}, max_{};
};
struct MeshReference {
    std::shared_ptr<const MeshAsset> data;
    std::string nativeId;
    std::string_view key() const {
        return !nativeId.empty() ? std::string_view(nativeId) :
               data ? std::string_view(data->digest()) : std::string_view{};
    }
    bool operator==(const MeshReference& other) const {
        return nativeId == other.nativeId &&
               (data == other.data || (data && other.data && data->digest() == other.data->digest()));
    }
};
} // namespace platz
