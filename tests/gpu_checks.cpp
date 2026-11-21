#include "renderer.hpp"
#include "Project.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <iomanip>
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

void profile(const std::filesystem::path& file) {
    const auto project = platz::Project::open(file);
    std::vector<DrawObject> scene;
    auto vector = [](platz::Vec3 v) -> Vec3 { return {v.x, v.y, v.z}; };
    for (const auto& object : project.objects()) {
        Mesh mesh{};
        switch (object.mesh) {
        case platz::Mesh::Cube: mesh = Mesh::Cube; break;
        case platz::Mesh::Sphere: mesh = Mesh::Sphere; break;
        case platz::Mesh::Cylinder: mesh = Mesh::Cylinder; break;
        }
        scene.push_back({static_cast<unsigned>(scene.size()) + 1, mesh, vector(object.position),
            vector(object.rotation), vector(object.scale), vector(object.color)});
    }
    TestSurface surface;
    check(surface.handle && SetWindowPos(surface.handle,nullptr,0,0,1280,720,SWP_NOZORDER|SWP_NOACTIVATE),
          "Could not size benchmark surface");
    Renderer renderer(surface.handle);
    renderer.resize(1280,720);
    const Camera camera;
    for (int i = 0; i < 16; ++i) renderer.draw(scene, camera);
    auto frame = renderer.capture();
    check(frame.width == 1280 && frame.height == 720, "Benchmark resolution mismatch");
    for (const auto& item : scene) check(coverage(frame, item.token) > 0, "Validation object disappeared");
    std::vector<double> cpu, gpu;
    constexpr int samples = 96;
    for (int i = 0; i < samples; ++i) {
        renderer.draw(scene, camera);
        cpu.push_back(renderer.lastCpuMilliseconds());
        gpu.push_back(renderer.lastGpuMilliseconds());
    }
    std::sort(cpu.begin(), cpu.end());
    std::sort(gpu.begin(), gpu.end());
    check(cpu.front() > 0 && gpu.front() > 0, "Device did not report all benchmark timestamps");
    std::cout << std::fixed << std::setprecision(4) << project.name() << ", source revision "
              << project.savedRevision() << ", " << scene.size() << " objects at 1280x720, 1x render scale, "
                 "16 warmup + 96 measured frames, static default camera, hardware FIFO, "
                 "no per-frame readback, hidden Win32 presentation surface\n"
              << "CPU submit/present p50/p95 " << cpu[47] << "/" << cpu[91] << " ms; "
              << "GPU timestamps p50/p95 " << gpu[47] << "/" << gpu[91] << " ms\n"
              << renderer.diagnostics() << '\n';
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
    // Recreate the swapchain after in-flight work, including a zero-sized viewport.
    for (int i = 0; i < 3; ++i) gpu.draw(scene, camera);
    gpu.resize(0,0);
    gpu.draw(scene, camera);
    check(gpu.pick(0,0) == 0 && gpu.capture().tokens.empty(), "Minimized viewport exposed stale IDs");
    check(SetWindowPos(surface.handle,nullptr,0,0,376,250,SWP_NOZORDER|SWP_NOACTIVATE), "Cannot resize test surface");
    gpu.resize(376,250);
    gpu.draw(scene, camera);
    const auto restored = gpu.capture();
    std::cout << "Restored viewport " << restored.width << 'x' << restored.height
              << ", shell coverage " << coverage(restored,shell.token) << " pixels\n";
    check(restored.width == 376 && restored.height == 250 && coverage(restored, shell.token) > 500,
          "Restored viewport lost its scene or attachments");
    check(gpu.validationErrors() == 0, "Vulkan reported graphics validation errors");
    std::cout << "Adapter: " << gpu.adapter() << "\n" << gpu.diagnostics() << "\n"
              << width << "x" << height << ", 3 objects, 11 draw + synchronous readbacks; median "
              << timings[5] << " ms, max " << timings.back() << " ms\n"
              << changed << " pixels changed solely on the edited mesh\n";
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--profile") profile(argv[2]);
        else if (argc == 1) run();
        else throw std::runtime_error("Expected no arguments or --profile <validation project>");
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
