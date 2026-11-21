#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace platz {

struct Vec3 {
    float x{}, y{}, z{};
    bool operator==(const Vec3&) const = default;
};
enum class Mesh { Cube, Sphere, Cylinder };
const char* meshName(Mesh mesh);

struct Object {
    std::string id, name;
    Mesh mesh = Mesh::Cube;
    Vec3 position{}, rotation{}, scale{1,1,1}, color{0.6f,0.7f,0.7f};
    bool operator==(const Object&) const = default;
};

struct ObjectEdit {
    std::string projectId, sceneId;
    std::uint64_t expectedRevision{};
    Object before, after;
};

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
    const std::vector<Object>& objects() const { return objects_; }
    const Object* find(const std::string& objectId) const;
    bool dirty() const;
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool canRestorePrevious() const { return hasPrevious_; }
    ObjectEdit stage(const Object& replacement, std::uint64_t expectedRevision) const;
    void apply(const ObjectEdit& proposal);
    void edit(const Object& replacement, std::uint64_t expectedRevision);
    std::string add(Mesh mesh);
    std::string duplicate(const std::string& objectId, std::uint64_t expectedRevision);
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
    std::vector<Object> objects_, saved_;
    std::vector<std::vector<Object>> undo_, redo_;
    bool hasPrevious_{};
    void replace(std::vector<Object> next);
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
