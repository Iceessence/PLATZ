#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace platz {
struct MeshVertex {
    std::array<float, 3> position, normal;
};

// Immutable source bytes and their rebuildable geometry travel together in memory.
class MeshAsset {
  public:
    static constexpr std::size_t maxBytes = 2u << 20;
    static std::shared_ptr<const MeshAsset> load(const std::filesystem::path&);
    static std::shared_ptr<const MeshAsset> decode(std::span<const std::uint8_t>);
    const std::string& digest() const { return digest_; }
    const auto& source() const { return source_; }
    const auto& vertices() const { return vertices_; }
    const auto& indices() const { return indices_; }
    const auto& minCorner() const { return min_; }
    const auto& maxCorner() const { return max_; }

  private:
    MeshAsset() = default;
    std::string digest_;
    std::vector<std::uint8_t> source_;
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::array<float, 3> min_{}, max_{};
};
struct MeshReference {
    std::shared_ptr<const MeshAsset> data;
    bool operator==(const MeshReference& other) const {
        return data == other.data || (data && other.data && data->digest() == other.data->digest());
    }
};
} // namespace platz
