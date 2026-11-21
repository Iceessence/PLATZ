#pragma once
#include "../assets/MeshAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace platz {

struct Vec3 {
    float x{}, y{}, z{};
    bool operator==(const Vec3&) const = default;
};
enum class Mesh { Cube, Sphere, Cylinder, Imported };
const char* meshName(Mesh mesh);

struct Object {
    std::string id, name;
    Mesh mesh = Mesh::Cube;
    Vec3 position{}, rotation{}, scale{1,1,1}, color{0.6f,0.7f,0.7f};
    MeshReference asset{};
    std::vector<SlotColor> slotColors;
    void setSlotColor(std::uint32_t slot, std::optional<std::array<float,3>> srgb);
    bool operator==(const Object&) const = default;
};

struct ObjectEdit {
    std::string projectId, sceneId;
    std::uint64_t expectedRevision{};
    Object before, after;
};

struct LibraryMesh {
    std::string name;
    MeshReference asset;
    bool operator==(const LibraryMesh&) const = default;
};
using MeshLibrary = std::map<std::string, LibraryMesh>;

class Project {
public:
    static Project create(const std::filesystem::path& path, const std::string& name);
    static Project open(const std::filesystem::path& path);
    const std::filesystem::path& path() const { return path_; }
    const std::string& id() const { return id_; }
    const std::string& sceneId() const { return sceneId_; }
    const std::string& name() const { return name_; }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t savedRevision() const { return head_; }
    const std::vector<Object>& objects() const { return scene_.objects; }
    const MeshLibrary& meshLibrary() const { return scene_.meshes; }
    const Object* find(const std::string& objectId) const;
    bool dirty() const;
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool canRestorePrevious() const { return hasPrevious_; }
    ObjectEdit stage(const Object& replacement, std::uint64_t expectedRevision) const;
    void apply(const ObjectEdit& proposal);
    void edit(const Object& replacement, std::uint64_t expectedRevision);
    std::string add(Mesh mesh);
    std::string importMesh(const std::filesystem::path& source, std::uint64_t expectedRevision);
    std::string importMeshAsset(const std::filesystem::path& source, std::uint64_t expectedRevision);
    std::string instantiateMesh(const std::string& digest, std::uint64_t expectedRevision);
    void assignMesh(const std::string& objectId, const std::string& digest, std::uint64_t expectedRevision);
    std::string replaceMeshInstances(const std::string& digest, const std::filesystem::path& source,
                                     std::uint64_t expectedRevision);
    void removeMeshAsset(const std::string& digest, std::uint64_t expectedRevision);
    std::string duplicate(const std::string& objectId, std::uint64_t expectedRevision);
    std::vector<Object> previewRepeat(const std::string& objectId, std::uint64_t expectedRevision,
                                      unsigned copies, Vec3 spacing) const;
    std::string repeat(const std::string& objectId, std::uint64_t expectedRevision, unsigned copies, Vec3 spacing);
    void remove(const std::string& objectId, std::uint64_t expectedRevision);
    void undo();
    void redo();
    void restorePrevious();
    void save();

private:
    Project() = default;
    std::filesystem::path path_;
    std::string id_, sceneId_, name_, diskToken_;
    std::uint64_t head_{}, revision_{};
    struct SceneState {
        std::vector<Object> objects;
        MeshLibrary meshes;
        bool operator==(const SceneState&) const = default;
    };
    SceneState scene_, saved_;
    std::vector<SceneState> undo_, redo_;
    bool hasPrevious_{};
    void replace(SceneState next);
    void requireRevision(std::uint64_t expectedRevision) const;
    void requireRevisionCapacity() const;
};

class Runtime {
public:
    explicit Runtime(const Project& source);
    const std::vector<Object>& objects() const { return objects_; }
    std::uint64_t sourceRevision() const { return sourceRevision_; }
    bool interact(const std::string& objectId);
private:
    std::vector<Object> objects_;
    std::uint64_t sourceRevision_;
};

}
