#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace platz::engine {
using Vec3 = std::array<float, 3>;
enum class Mesh { Cube, Sphere, Cylinder };

struct DrawObject {
    std::uint32_t token{}; // Nonzero. Owned by the caller's scene-to-frame mapping.
    Mesh mesh{Mesh::Cube};
    Vec3 position{};
    Vec3 rotation{}; // Degrees, local X then Y then Z, right-handed.
    Vec3 scale{1, 1, 1};
    Vec3 color{0.65f, 0.65f, 0.65f}; // sRGB
};

struct Camera {
    Vec3 target{0, 0.85f, 0};
    float yaw{0.58f}, pitch{0.39f}, distance{10.2f}; // Radians, metres
    void orbit(float deltaX, float deltaY); // Drag distance in client pixels.
    void zoom(float wheelSteps);
    void pan(float deltaX, float deltaY, float viewportHeight);
    void move(float right, float up, float forward); // World distances.
    void frame(Vec3 center, Vec3 dimensions);
};

struct Capture {
    unsigned width{}, height{};
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint32_t> tokens;
};

class Renderer {
public:
    explicit Renderer(void* nativeWindow); // Windows HWND; throws std::runtime_error on failure.
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(unsigned width, unsigned height);
    void draw(std::span<const DrawObject> objects, const Camera& camera, std::uint32_t selected = 0);
    std::uint32_t pick(int x, int y); // Physical client coordinates of the last frame.
    Capture capture(); // Explicit synchronous readback of the last frame.
    const std::string& adapter() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
