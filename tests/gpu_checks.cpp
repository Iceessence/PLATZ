#include "renderer.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace platz::engine;

namespace {
constexpr unsigned width = 432, height = 288;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TestSurface {
    HWND handle = CreateWindowExW(0, L"STATIC", L"PLATZ hardware test", WS_POPUP,
        0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~TestSurface() { if (handle) DestroyWindow(handle); }
};

std::size_t coverage(const Capture& frame, std::uint32_t token) {
    return std::count(frame.tokens.begin(), frame.tokens.end(), token);
}

void run() {
    TestSurface surface;
    check(surface.handle, "Windows test surface creation failed");
    Renderer gpu(surface.handle);
    gpu.resize(width, height);
    Camera camera;
    camera.target = {0, 2, 0};
    camera.distance = 8;

    DrawObject shell{101, Mesh::Cube, camera.target, {}, {2,2,2}, {0.85f,0.2f,0.1f}};
    DrawObject inside{203, Mesh::Sphere, camera.target, {}, {0.8f,0.8f,0.8f}, {0.1f,0.8f,0.2f}};
    DrawObject side{307, Mesh::Cylinder, {2.7f,2,0}, {}, {1,1,1}, {0.1f,0.3f,0.9f}};
    std::array scene{shell, inside, side};
    gpu.draw(scene, camera);
    auto frame = gpu.capture();
    check(frame.width == width && frame.height == height &&
          frame.tokens.size() == width * height && frame.rgba.size() == width * height * 4,
          "Readback dimensions or row pitch are incorrect");
    check(coverage(frame, shell.token) > 600 && coverage(frame, side.token) > 80 &&
          !coverage(frame, inside.token), "Occlusion or object-ID attachment is incorrect");
    check(gpu.pick(width / 2, height / 2) == shell.token, "Center pick missed enclosing mesh");
    auto sidePixel = std::find(frame.tokens.begin(), frame.tokens.end(), side.token) - frame.tokens.begin();
    check(gpu.pick(int(sidePixel % width), int(sidePixel / width)) == side.token,
          "Pointer pick disagrees with presented identity attachment");

    std::reverse(scene.begin(), scene.end());
    gpu.draw(scene, camera);
    const auto reversed = gpu.capture();
    check(reversed.tokens == frame.tokens && reversed.rgba == frame.rgba,
          "Opaque depth and colors depend on submission order");

    scene.back().color = {0.1f,0.8f,0.9f};
    gpu.draw(scene, camera);
    auto recolored = gpu.capture();
    check(recolored.tokens == frame.tokens, "Material edit changed geometry");
    std::size_t changed = 0;
    for (std::size_t i = 0; i < frame.tokens.size(); ++i) {
        const auto a = frame.rgba.begin() + i * 4, b = recolored.rgba.begin() + i * 4;
        if (!std::equal(a, a + 4, b)) {
            check(frame.tokens[i] == shell.token, "Material edit changed another object");
            ++changed;
        }
    }
    check(changed > 600, "Material edit failed to update the actual rendered color");
    Camera zoomed = camera;
    zoomed.distance *= 0.75f;
    gpu.draw(scene, zoomed);
    check(coverage(gpu.capture(), shell.token) > coverage(frame, shell.token) * 1.3,
          "Dolly toward target did not increase perspective coverage");
    camera.orbit(88, 20);
    gpu.draw(scene, camera);
    check(gpu.capture().tokens != frame.tokens, "Orbit failed to change projected geometry");

    // Each timing includes GPU completion and a full ID/color readback. This deliberately
    // measures the test workload and is unsuitable as an interactive frame-rate benchmark.
    std::array<double, 11> timings{};
    for (auto& time : timings) {
        const auto start = std::chrono::steady_clock::now();
        gpu.draw(scene, camera);
        gpu.capture();
        time = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    std::sort(timings.begin(), timings.end());
    std::cout << "Adapter: " << gpu.adapter() << "\n"
              << width << "x" << height << ", 3 objects, 11 draw + synchronous readbacks; median "
              << timings[5] << " ms, max " << timings.back() << " ms\n"
              << changed << " pixels changed solely on the edited mesh\n";
}
}

int main() {
    try { run(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
