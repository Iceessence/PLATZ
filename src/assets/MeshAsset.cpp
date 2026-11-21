#include "MeshAsset.hpp"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace platz {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string digestOf(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char, 32> hash{};
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(bytes.size()), hash.data(), static_cast<ULONG>(hash.size())) >= 0,
            "Could not hash the mesh source.");
    std::string out;
    for (auto b : hash) {
        out.push_back("0123456789abcdef"[b >> 4]);
        out.push_back("0123456789abcdef"[b & 15]);
    }
    return out;
}
std::uint32_t word(std::span<const std::uint8_t> bytes, size_t at) {
    std::uint32_t result{};
    std::memcpy(&result, bytes.data() + at, 4);
    return result;
}
void checkJson(std::span<const std::uint8_t> json) {
    int depth = 0;
    bool quoted = false, escape = false;
    for (auto c : json) {
        if (quoted) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[')
            require(++depth <= 48, "GLB metadata is too deeply nested.");
        else if (c == '}' || c == ']')
            require(--depth >= 0, "Malformed GLB metadata.");
    }
    require(!quoted && !depth, "Incomplete GLB metadata.");
}
void checkAccessor(const cgltf_accessor& a, size_t bytes, size_t alignment, size_t maxCount) {
    auto* v = a.buffer_view;
    require(v && !a.is_sparse && !a.normalized && !v->has_meshopt_compression && a.count &&
                a.count <= maxCount && a.stride >= bytes && a.stride <= 252 && a.stride % alignment == 0 &&
                a.offset <= v->size && (v->offset % alignment + a.offset % alignment) % alignment == 0 &&
                bytes <= v->size - a.offset && a.count - 1 <= (v->size - a.offset - bytes) / a.stride,
            "Unsupported or out-of-bounds GLB accessor.");
}
} // namespace
std::shared_ptr<const MeshAsset> MeshAsset::load(const std::filesystem::path& file) {
    require(std::filesystem::is_regular_file(file), "Select a local GLB file.");
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    auto length = stream.tellg();
    require(length > 0 && length <= maxBytes, "Select a GLB of at most 2 MiB.");
    std::vector<std::uint8_t> source(static_cast<size_t>(length));
    stream.seekg(0);
    require(bool(stream.read(reinterpret_cast<char*>(source.data()), source.size())),
            "Could not read the GLB.");
    return decode(source);
}
std::shared_ptr<const MeshAsset> MeshAsset::decode(std::span<const std::uint8_t> source) {
    require(source.size() >= 28 && source.size() <= maxBytes && word(source, 0) == 0x46546c67 &&
                word(source, 4) == 2 && word(source, 8) == source.size() && word(source, 16) == 0x4e4f534a,
            "Invalid GLB 2.0 header.");
    const auto jsonSize = word(source, 12);
    require(jsonSize % 4 == 0 && jsonSize <= 128 * 1024 && jsonSize <= source.size() - 28,
            "Unsupported GLB JSON chunk.");
    checkJson(source.subspan(20, jsonSize));
    const size_t binHeader = 20 + jsonSize;
    require(word(source, binHeader + 4) == 0x004e4942 &&
                word(source, binHeader) == source.size() - binHeader - 8,
            "An embedded binary mesh buffer is required.");
    cgltf_options options{};
    size_t remaining = 16u << 20;
    options.memory.user_data = &remaining;
    options.memory.alloc_func = [](void* data, size_t size) -> void* {
        auto& left = *static_cast<size_t*>(data);
        if (size > left)
            return nullptr;
        left -= size;
        return std::malloc(size);
    };
    options.memory.free_func = [](void*, void* ptr) { std::free(ptr); };
    cgltf_data* raw{};
    require(cgltf_parse(&options, source.data(), source.size(), &raw) == cgltf_result_success,
            "Invalid glTF metadata.");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, &cgltf_free);
    require(raw->asset.version && std::string_view(raw->asset.version) == "2.0",
            "Only glTF 2.0 mesh sources are supported.");
    require(raw->bin && raw->buffers_count == 1 && !raw->buffers[0].uri && raw->buffers[0].size <= raw->bin_size &&
                raw->meshes_count == 1 && raw->meshes[0].primitives_count == 1 &&
                !raw->extensions_required_count && !raw->skins_count && !raw->animations_count,
            "Import requires one static mesh and one embedded buffer.");
    for (size_t i = 0; i < raw->buffer_views_count; ++i) {
        auto& v = raw->buffer_views[i];
        require(v.buffer == raw->buffers && v.offset <= v.buffer->size && v.size <= v.buffer->size - v.offset,
                "GLB buffer view exceeds its buffer.");
    }
    auto& primitive = raw->meshes[0].primitives[0];
    require(primitive.type == cgltf_primitive_type_triangles && primitive.indices &&
                !primitive.targets_count && !primitive.has_draco_mesh_compression,
            "Import requires indexed, uncompressed triangles.");
    auto* pos = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
    auto* normal = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
    auto* idx = primitive.indices;
    require(pos && pos->type == cgltf_type_vec3 && pos->component_type == cgltf_component_type_r_32f,
            "Float VEC3 positions are required.");
    checkAccessor(*pos, 12, 4, 65536);
    if (normal) {
        require(normal->type == pos->type && normal->component_type == pos->component_type &&
                    normal->count == pos->count,
                "Invalid normal attribute.");
        checkAccessor(*normal, 12, 4, 65536);
    }
    require(idx->type == cgltf_type_scalar && idx->count % 3 == 0 &&
                (idx->component_type == cgltf_component_type_r_8u ||
                 idx->component_type == cgltf_component_type_r_16u ||
                 idx->component_type == cgltf_component_type_r_32u),
            "Unsigned triangle indices are required.");
    const auto width = cgltf_component_size(idx->component_type);
    checkAccessor(*idx, width, width, 196608);
    // The source span owns the bytes. Bind only the checked BIN chunk and never resolve a URI.
    raw->buffers[0].data = const_cast<void*>(raw->bin);
    auto asset = std::shared_ptr<MeshAsset>(new MeshAsset);
    asset->source_.assign(source.begin(), source.end());
    asset->digest_ = digestOf(source);
    asset->vertices_.resize(pos->count);
    asset->indices_.reserve(idx->count);
    for (size_t i = 0; i < pos->count; ++i) {
        auto& v = asset->vertices_[i];
        cgltf_accessor_read_float(pos, i, v.position.data(), 3);
        for (float x : v.position)
            require(std::isfinite(x) && std::abs(x) <= 100000, "Invalid mesh coordinate.");
        for (size_t k = 0; k < 3; ++k) {
            asset->min_[k] = i ? std::min(asset->min_[k], v.position[k]) : v.position[k];
            asset->max_[k] = i ? std::max(asset->max_[k], v.position[k]) : v.position[k];
        }
        if (normal)
            cgltf_accessor_read_float(normal, i, v.normal.data(), 3);
    }
    for (size_t i = 0; i < idx->count; i += 3) {
        std::array<size_t, 3> triangle{};
        for (size_t j = 0; j < 3; ++j) {
            triangle[j] = cgltf_accessor_read_index(idx, i + j);
            require(triangle[j] < pos->count, "Mesh index is outside the vertex array.");
            asset->indices_.push_back(static_cast<uint32_t>(triangle[j]));
        }
        if (!normal) {
            auto a = asset->vertices_[triangle[0]].position, b = asset->vertices_[triangle[1]].position,
                 c = asset->vertices_[triangle[2]].position;
            for (int k = 0; k < 3; ++k) {
                b[k] -= a[k];
                c[k] -= a[k];
            }
            for (auto index : triangle)
                for (int k = 0; k < 3; ++k)
                    asset->vertices_[index].normal[k] +=
                        b[(k + 1) % 3] * c[(k + 2) % 3] - b[(k + 2) % 3] * c[(k + 1) % 3];
        }
    }
    for (auto& v : asset->vertices_) {
        float length = std::hypot(v.normal[0], v.normal[1], v.normal[2]);
        require(std::isfinite(length), "Invalid vertex normal.");
        if (length > 0)
            for (float& x : v.normal)
                x /= length;
        else
            v.normal = {0, 1, 0};
    }
    return asset;
}
} // namespace platz
