#include "MeshAsset.hpp"
#include "AssetDigest.hpp"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace platz {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
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
void checkAccessor(const cgltf_accessor& a, size_t bytes, size_t alignment, size_t maxCount,
                   bool normalized = false) {
    auto* v = a.buffer_view;
    require(v && !a.is_sparse && a.normalized == normalized && !v->has_meshopt_compression && a.count &&
                a.count <= maxCount && a.stride >= bytes && a.stride <= 252 && a.stride % alignment == 0 &&
                a.offset <= v->size && (v->offset % alignment + a.offset % alignment) % alignment == 0 &&
                bytes <= v->size - a.offset && a.count - 1 <= (v->size - a.offset - bytes) / a.stride,
            "Unsupported or out-of-bounds GLB accessor.");
}
TextureWrap addressMode(cgltf_wrap_mode mode) {
    if (mode == cgltf_wrap_mode_clamp_to_edge)
        return TextureWrap::Clamp;
    if (mode == cgltf_wrap_mode_mirrored_repeat)
        return TextureWrap::Mirror;
    return TextureWrap::Repeat;
}
TextureFilter filterMode(cgltf_filter_type mode) {
    if (mode == cgltf_filter_type_nearest || mode == cgltf_filter_type_nearest_mipmap_nearest ||
        mode == cgltf_filter_type_nearest_mipmap_linear)
        return TextureFilter::Nearest;
    return TextureFilter::Linear;
}
BaseColorTexture readTexture(const cgltf_texture_view& view, std::span<const std::uint8_t> source,
                             size_t binaryOffset) {
    require(view.texcoord == 0 && !view.has_transform,
            "This texture requires an unsupported UV set or transform.");
    const auto* image = view.texture->image;
    require(image && image->buffer_view && !image->uri && image->mime_type &&
                !image->buffer_view->has_meshopt_compression,
            "Use an embedded PNG or JPEG for the base-color texture.");
    BaseColorTexture texture;
    const auto mime = std::string_view(image->mime_type);
    require(mime == "image/png" || mime == "image/jpeg",
            "Only embedded PNG and JPEG textures are supported.");
    texture.encoding = mime == "image/png" ? TextureEncoding::Png : TextureEncoding::Jpeg;
    texture.offset = binaryOffset + image->buffer_view->offset;
    texture.length = image->buffer_view->size;
    require(texture.offset <= source.size() && texture.length <= source.size() - texture.offset,
            "Embedded texture is outside the GLB buffer.");
    const auto size = inspectTexture(source.subspan(texture.offset, texture.length), texture.encoding);
    texture.width = size[0];
    texture.height = size[1];
    texture.mip = TextureMipFilter::Linear;
    if (const auto* sampler = view.texture->sampler) {
        texture.wrapU = addressMode(sampler->wrap_s);
        texture.wrapV = addressMode(sampler->wrap_t);
        texture.min = filterMode(sampler->min_filter);
        texture.mag = filterMode(sampler->mag_filter);
        switch (sampler->min_filter) {
        case cgltf_filter_type_nearest:
        case cgltf_filter_type_linear: texture.mip=TextureMipFilter::None; break;
        case cgltf_filter_type_nearest_mipmap_nearest:
        case cgltf_filter_type_linear_mipmap_nearest: texture.mip=TextureMipFilter::Nearest; break;
        default: break;
        }
    }
    return texture;
}
MeshMaterialSlot readSlot(const cgltf_material* material, std::span<const std::uint8_t> source,
                          size_t binaryOffset) {
    MeshMaterialSlot slot;
    if (!material)
        return slot;
    if (material->name) {
        const std::string_view name(material->name);
        auto end = std::min<std::size_t>(name.size(), 128);
        if (end < name.size())
            while ((static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
                --end;
        slot.name = name.substr(0, end);
    }
    auto& color = slot.sourceColor.emplace();
    for (size_t channel = 0; channel < color.size(); ++channel) {
        const float linear = material->pbr_metallic_roughness.base_color_factor[channel];
        require(std::isfinite(linear) && linear >= 0 && linear <= 1, "Invalid GLB base color factor.");
        color[channel] = linear <= 0.0031308f
                             ? 12.92f * linear
                             : std::clamp(1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f, 0.f, 1.f);
    }
    const auto& texture = material->pbr_metallic_roughness.base_color_texture;
    if (texture.texture) {
        try {
            slot.texture = readTexture(texture, source, binaryOffset);
        } catch (const std::runtime_error& error) {
            slot.textureNote = error.what();
        }
    }
    return slot;
}

bool readUv(const cgltf_accessor* uv, std::span<MeshVertex> vertices) {
    if (!uv || uv->type != cgltf_type_vec2 || uv->count != vertices.size())
        return false;
    const auto type = uv->component_type;
    if (type != cgltf_component_type_r_32f && type != cgltf_component_type_r_8u &&
        type != cgltf_component_type_r_16u)
        return false;
    const auto size = cgltf_component_size(type);
    try {
        checkAccessor(*uv, 2 * size, size, vertices.size(), type != cgltf_component_type_r_32f);
    } catch (const std::runtime_error&) {
        return false;
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        auto& point = vertices[i].uv;
        if (!cgltf_accessor_read_float(uv, i, point.data(), 2) || !std::isfinite(point[0]) ||
            !std::isfinite(point[1]) || std::abs(point[0]) > 100000 || std::abs(point[1]) > 100000)
            return false;
    }
    return true;
}
bool appendPrimitive(const cgltf_primitive& primitive, std::vector<MeshVertex>& vertices,
                     std::vector<std::uint32_t>& indices) {
    require(primitive.type == cgltf_primitive_type_triangles && primitive.indices &&
                !primitive.targets_count && !primitive.has_draco_mesh_compression,
            "Import requires indexed, uncompressed triangles.");
    auto* pos = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
    auto* normal = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
    auto* idx = primitive.indices;
    require(pos && pos->type == cgltf_type_vec3 && pos->component_type == cgltf_component_type_r_32f,
            "Float VEC3 positions are required.");
    checkAccessor(*pos, 12, 4, 65536 - vertices.size());
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
    checkAccessor(*idx, width, width, 196608 - indices.size());

    const auto base = static_cast<std::uint32_t>(vertices.size());
    vertices.resize(base + pos->count);
    for (size_t i = 0; i < pos->count; ++i) {
        auto& v = vertices[base + i];
        cgltf_accessor_read_float(pos, i, v.position.data(), 3);
        for (float x : v.position)
            require(std::isfinite(x) && std::abs(x) <= 100000, "Invalid mesh coordinate.");
        if (normal)
            cgltf_accessor_read_float(normal, i, v.normal.data(), 3);
    }
    for (size_t i = 0; i < idx->count; i += 3) {
        std::array<std::uint32_t, 3> triangle{};
        for (size_t j = 0; j < triangle.size(); ++j) {
            const auto index = cgltf_accessor_read_index(idx, i + j);
            require(index < pos->count, "Mesh index is outside the vertex array.");
            triangle[j] = base + static_cast<std::uint32_t>(index);
            indices.push_back(triangle[j]);
        }
        if (!normal) {
            const auto a = vertices[triangle[0]].position;
            auto b = vertices[triangle[1]].position;
            auto c = vertices[triangle[2]].position;
            for (int k = 0; k < 3; ++k) {
                b[k] -= a[k];
                c[k] -= a[k];
            }
            for (auto index : triangle)
                for (int k = 0; k < 3; ++k)
                    vertices[index].normal[k] +=
                        b[(k + 1) % 3] * c[(k + 2) % 3] - b[(k + 2) % 3] * c[(k + 1) % 3];
        }
    }
    for (auto& v : std::span(vertices).subspan(base)) {
        const float length = std::hypot(v.normal[0], v.normal[1], v.normal[2]);
        require(std::isfinite(length), "Invalid vertex normal.");
        if (length > 0)
            for (float& x : v.normal)
                x /= length;
        else
            v.normal = {0, 1, 0};
    }
    auto current = std::span(vertices).subspan(base);
    const auto hasUv = readUv(cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0), current);
    if (!hasUv)
        for (auto& vertex : current)
            vertex.uv = {};
    return hasUv;
}
} // namespace
bool MeshAsset::supportsImageSlot(unsigned slot) const {
    return slot<slots_.size() && std::none_of(primitives_.begin(),primitives_.end(),[slot](const auto& primitive){
        return primitive.slot==slot && !primitive.hasUv;
    });
}
const BaseColorTexture* MeshAsset::textureForSlot(std::uint32_t slot,
                                                  std::span<const std::uint32_t> disabled) const {
    const auto& texture = slots_.at(slot).texture;
    if (!texture || std::find(disabled.begin(), disabled.end(), slot) != disabled.end())
        return nullptr;
    return &*texture;
}
void MeshAsset::readTexturePixels(const BaseColorTexture& texture, std::span<std::uint8_t> rgba) const {
    if (texture.offset > source_.size() || texture.length > source_.size() - texture.offset)
        throw std::runtime_error("Invalid texture byte range.");
    decodeTexture(std::span(source_).subspan(texture.offset, texture.length), texture, rgba);
}
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
    require(raw->bin && raw->buffers_count == 1 && !raw->buffers[0].uri &&
                raw->buffers[0].size <= raw->bin_size && raw->meshes_count == 1 &&
                raw->meshes[0].primitives_count > 0 && raw->meshes[0].primitives_count <= maxPrimitives &&
                !raw->extensions_required_count && !raw->skins_count && !raw->animations_count,
            "Import requires one static mesh with up to 32 primitives and one embedded buffer.");
    for (size_t i = 0; i < raw->buffer_views_count; ++i) {
        auto& v = raw->buffer_views[i];
        require(v.buffer == raw->buffers && v.offset <= v.buffer->size && v.size <= v.buffer->size - v.offset,
                "GLB buffer view exceeds its buffer.");
    }
    // The source span owns the bytes. Bind only the checked BIN chunk and never resolve a URI.
    raw->buffers[0].data = const_cast<void*>(raw->bin);
    auto asset = std::shared_ptr<MeshAsset>(new MeshAsset);
    asset->source_.assign(source.begin(), source.end());
    asset->digest_ = assetDigest(source);
    std::map<const cgltf_material*, std::uint32_t> slots;
    const auto& mesh = raw->meshes[0];
    for (size_t i = 0; i < mesh.primitives_count; ++i) {
        const auto& primitive = mesh.primitives[i];
        auto [slot, inserted] =
            slots.try_emplace(primitive.material, static_cast<std::uint32_t>(slots.size()));
        if (inserted)
            asset->slots_.push_back(readSlot(primitive.material, source, binHeader + 8));
        const auto firstIndex = static_cast<std::uint32_t>(asset->indices_.size());
        const bool hasUv = appendPrimitive(primitive, asset->vertices_, asset->indices_);
        asset->primitives_.push_back({firstIndex,
                                      static_cast<std::uint32_t>(asset->indices_.size()) - firstIndex,
                                      slot->second, hasUv});
    }
    for (std::uint32_t i = 0; i < asset->slots_.size(); ++i) {
        auto& slot = asset->slots_[i];
        const bool mapped = std::any_of(
            asset->primitives_.begin(), asset->primitives_.end(),
            [i](const MeshPrimitive& primitive) { return primitive.slot == i && primitive.hasUv; });
        if (slot.texture && !mapped) {
            slot.texture.reset();
            slot.textureNote = "No usable TEXCOORD_0 mapping on this slot.";
        }
    }
    asset->min_ = asset->max_ = asset->vertices_.front().position;
    for (const auto& v : asset->vertices_) {
        for (size_t axis = 0; axis < 3; ++axis) {
            asset->min_[axis] = std::min(asset->min_[axis], v.position[axis]);
            asset->max_[axis] = std::max(asset->max_[axis], v.position[axis]);
        }
    }
    return asset;
}
} // namespace platz
