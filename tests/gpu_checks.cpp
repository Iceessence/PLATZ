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
        case platz::Mesh::Imported: mesh = Mesh::Imported; break;
        }
        scene.push_back({static_cast<unsigned>(scene.size()) + 1, mesh, vector(object.position),
            vector(object.rotation), vector(object.scale), vector(object.color),object.asset.data,object.slotColors,
            object.texturesOff,object.images});
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
    auto distant=shell;
    distant.position={10000,2,0};
    distant.token=409;
    const std::array spread{shell,inside,side,distant};
    gpu.draw(spread,camera);
    const auto clipped=gpu.capture();
    check(clipped.tokens==frame.tokens && clipped.rgba==frame.rgba &&
          gpu.diagnostics().find("| objects culled 1 |")!=std::string::npos,
          "Offscreen object altered the rendered scene or was submitted");
    auto remoteCamera=camera;
    remoteCamera.target=distant.position;
    gpu.draw(spread,remoteCamera);
    check(gpu.pick(width/2,height/2)==distant.token,"Camera movement did not restore a previously culled object");
    auto edgeCamera=camera;
    edgeCamera.yaw=edgeCamera.pitch=0;
    auto edge=shell;
    edge.position[0]=5.5f;
    gpu.draw(std::span(&edge,1),edgeCamera);
    check(coverage(gpu.capture(),edge.token)>0,"Partly visible cube disappeared at the frustum boundary");
    gpu.draw(scene,camera);
    check(gpu.requestPick(int(sidePixel%width),int(sidePixel/width)),"Asynchronous selection could not queue the ID pixel");
    check(!gpu.requestPick(width/2,height/2),"A pending selection was overwritten");
    auto changedIds=scene;
    for (auto& object : changedIds) object.token+=1000;
    gpu.draw(changedIds,camera);
    gpu.capture(); // Explicit test-only synchronization with the intervening frame.
    check(gpu.pollPick()==side.token && !gpu.pollPick(),"Asynchronous selection did not retain the clicked frame identity");

    std::reverse(scene.begin(), scene.end());
    gpu.draw(scene, camera);
    const auto reversed = gpu.capture();
    check(reversed.tokens == frame.tokens && reversed.rgba == frame.rgba,
          "Opaque depth and colors depend on submission order");

    const std::array<OverlayVertex,3> overlay{{
        {{-.25f,-.25f},{.4f,1.f,.9f}},
        {{.25f,-.25f},{.4f,1.f,.9f}},
        {{0,.25f},{.4f,1.f,.9f}}
    }};
    gpu.draw(scene,camera,0,overlay);
    const auto withOverlay=gpu.capture();
    check(withOverlay.tokens==frame.tokens && withOverlay.rgba!=frame.rgba &&
          gpu.pick(width/2,height/2)==shell.token,"Overlay changed picking or failed to draw");
    auto fadedOverlay=overlay;
    for (auto& vertex : fadedOverlay) vertex.depth=1.f;
    gpu.draw(scene,camera,0,fadedOverlay);
    const auto behind=gpu.capture();
    const size_t center=(height/2*width+width/2)*4;
    const auto effect=[&](const Capture& capture) {
        int change=0;
        for (size_t i=center;i<center+3;++i) change+=std::abs(int(capture.rgba[i])-frame.rgba[i]);
        return change;
    };
    check(effect(withOverlay)>30 && std::abs(effect(behind)-.3*effect(withOverlay))<4 && behind.tokens==frame.tokens,
          "Depth-occluded overlay did not preserve IDs and blend at 30 percent");
    for (auto& vertex : fadedOverlay) {vertex.depth=0.f; vertex.opacity=.45f;}
    gpu.draw(scene,camera,0,fadedOverlay);
    check(std::abs(effect(gpu.capture())-.45*effect(withOverlay))<4,"Overlay vertex coverage did not blend with scene color");

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
    DrawObject mesh{421,Mesh::Imported,{0,0,0}};
    mesh.asset=platz::MeshAsset::load(PLATZ_TEST_MESH);
    Camera meshCamera;
    meshCamera.frameObject(mesh,376.f/250.f);
    gpu.draw(std::span(&mesh,1),meshCamera);
    check(coverage(gpu.capture(),mesh.token)>300,"Imported mesh was not uploaded");
    gpu.draw(scene,camera);
    check(gpu.capture().tokens==restored.tokens,"Retiring the imported atlas changed built-in geometry");
    gpu.draw(std::span(&mesh,1),meshCamera);
    check(coverage(gpu.capture(),mesh.token)>300,"Re-uploading an imported atlas failed");

    mesh.asset=platz::MeshAsset::load(PLATZ_TEST_MATERIALS);
    meshCamera.yaw=0;
    meshCamera.frameObject(mesh,376.f/250.f);
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto originalColors=gpu.capture();
    mesh.slotColors={{0,{.25f,.15f,.92f}}};
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto editedColors=gpu.capture();
    check(originalColors.tokens==editedColors.tokens,"Material changes disturbed object picking.");
    int changesOnLeft=0, changesOnRight=0, untouchedMeshPixels=0;
    for (size_t i=0;i<originalColors.tokens.size();++i) {
        const auto initial=originalColors.rgba.begin()+4*i;
        const auto result=editedColors.rgba.begin()+4*i;
        if (!std::equal(initial,initial+4,result)) {
            check(originalColors.tokens[i]==mesh.token && initial[0]>initial[1]+24 && initial[0]>initial[2]+24,
                  "Material edit recolored a pixel outside the Copper slot.");
            if (i%editedColors.width<editedColors.width/2) ++changesOnLeft;
            else ++changesOnRight;
        } else if (originalColors.tokens[i]==mesh.token) {
            ++untouchedMeshPixels;
        }
    }
    check(changesOnLeft>100 && changesOnRight>100 && untouchedMeshPixels>100,
          "Both Copper primitives and the untouched Lagoon primitive were not rendered.");
    std::cout<<"Shared material slot recolored separated primitives without changing the Lagoon slot or object IDs\n";

    mesh.asset=platz::MeshAsset::load(PLATZ_TEST_TEXTURE);
    mesh.slotColors.clear();
    meshCamera.pitch=0;
    meshCamera.frameObject(mesh,376.f/250.f);
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto textured=gpu.capture();
    std::size_t topRed=0, bottomBlue=0, jpegBlue=0, jpegWhite=0;
    for (size_t i=0;i<textured.tokens.size();++i) {
        if (textured.tokens[i]!=mesh.token) continue;
        const auto* rgb=&textured.rgba[4*i];
        const auto x=i%textured.width, y=i/textured.width;
        if (x<textured.width/2) {
            if (y<textured.height/2 && rgb[0]>rgb[1]+80 && rgb[0]>rgb[2]+80) ++topRed;
            if (y>=textured.height/2 && rgb[2]>rgb[1]+60 && rgb[2]>rgb[0]+80) ++bottomBlue;
        } else {
            if (rgb[2]>rgb[1]+65 && rgb[2]>rgb[0]+85) ++jpegBlue;
            if (rgb[0]>160 && rgb[1]>160 && rgb[2]>160) ++jpegWhite;
        }
    }
    check(topRed>100 && bottomBlue>100 && jpegBlue>100 && jpegWhite>100 && gpu.textureWarnings()==0,
          "Embedded PNG orientation, normalized UVs or JPEG colors were not rendered.");
    mesh.texturesOff={0};
    mesh.slotColors={{0,{240.f/255,32.f/255,28.f/255}}};
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto flatRed=gpu.capture();
    check(flatRed.tokens==textured.tokens && flatRed.rgba!=textured.rgba,"Texture toggle disturbed object IDs.");
    std::size_t matchingReds=0;
    for (size_t i=0;i<textured.tokens.size();++i) {
        const auto* rgb=&textured.rgba[4*i];
        const auto* flat=&flatRed.rgba[4*i];
        if (i%textured.width>=textured.width/2)
            check(std::equal(rgb,rgb+4,flat),"Disabling one texture changed the JPEG slot.");
        else if (textured.tokens[i]==mesh.token && rgb[0]>rgb[1]+80 && rgb[0]>rgb[2]+80 &&
                 std::abs(rgb[0]-flat[0])<=2 && std::abs(rgb[1]-flat[1])<=2 && std::abs(rgb[2]-flat[2])<=2)
            ++matchingReds;
    }
    check(matchingReds>100,"sRGB texture sampling differs from the same flat albedo under identical lighting.");
    mesh.texturesOff.clear();
    mesh.slotColors.clear();
    gpu.draw(std::span(&mesh,1),meshCamera);
    check(gpu.capture().rgba==textured.rgba,"Re-uploading a texture changed its rendered pixels.");

    mesh.texturesOff={0};
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto flatFallback=gpu.capture();
    auto corrupted=mesh.asset->source();
    const auto& png=*mesh.asset->materialSlots()[0].texture;
    const auto section=std::span(corrupted).subspan(png.offset,png.length);
    const std::array<std::uint8_t,4> idat{'I','D','A','T'};
    const auto data=std::search(section.begin(),section.end(),idat.begin(),idat.end());
    check(data!=section.end() && (section.end()-data)>10,"Fixture PNG has no IDAT payload.");
    data[4]=0;
    mesh.asset=platz::MeshAsset::decode(corrupted);
    mesh.texturesOff.clear();
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto recovered=gpu.capture();
    check(recovered.tokens==flatFallback.tokens && recovered.rgba==flatFallback.rgba,
          "A corrupt raster affected the viewport or the unaffected material.");
    if (mesh.asset->materialSlots()[0].texture)
        check(gpu.textureWarnings()==1,"A GPU upload decode fallback was not surfaced.");
    std::cout<<"PNG/JPEG orientation, UV0, sRGB sampling, per-slot isolation, texture re-upload and corrupt-raster fallback passed\n";

    mesh.asset=platz::MeshAsset::load(PLATZ_TEST_TEXTURE);
    mesh.images={{0,"project image",platz::RasterImage::create(2,2,{
        0,240,0,255, 0,240,0,255, 0,240,0,255, 0,240,0,255})}};
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto projectImage=gpu.capture();
    check(projectImage.tokens==textured.tokens && projectImage.rgba!=textured.rgba,
          "Assigned RGBA image did not replace the embedded base color.");
    std::size_t greenPixels=0;
    for (size_t i=0;i<projectImage.tokens.size();++i) {
        const auto* rgb=&projectImage.rgba[4*i];
        if (i%projectImage.width>=projectImage.width/2)
            check(std::equal(rgb,rgb+4,&textured.rgba[4*i]),"Project image replaced the neighboring texture.");
        else if (projectImage.tokens[i]==mesh.token && rgb[1]>rgb[0]+75 && rgb[1]>rgb[2]+75)
            ++greenPixels;
    }
    check(greenPixels>100,"Native RGBA pixels were not sampled on the assigned slot.");
    mesh.images.front().raster=platz::RasterImage::create(2,2,{
        0,240,0,0, 0,240,0,0, 0,240,0,0, 0,240,0,0});
    gpu.draw(std::span(&mesh,1),meshCamera);
    check(gpu.capture().rgba==projectImage.rgba,"Opaque material unexpectedly blended native alpha.");
    mesh.images.clear();
    gpu.draw(std::span(&mesh,1),meshCamera);
    check(gpu.capture().rgba==textured.rgba,"Detaching a native image failed to restore the embedded texture.");
    std::cout<<"Project RGBA texture upload, opaque alpha behavior and per-slot isolation passed\n";

    auto cylinder=platz::defaultNativeMesh(platz::NativeShape::Cylinder);
    cylinder.size={1,1.6f,1};
    cylinder.topScale=.45f;
    mesh.asset=platz::buildNativeMesh(cylinder);
    mesh.slotColors={{0,{.25f,.72f,.5f}}};
    meshCamera.frameObject(mesh,float(width)/height);
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto taper=gpu.capture();
    cylinder.topScale=1.5f;
    mesh.asset=platz::buildNativeMesh(cylinder);
    gpu.draw(std::span(&mesh,1),meshCamera);
    const auto expanded=gpu.capture();
    check(coverage(taper,mesh.token)>300 && coverage(expanded,mesh.token)>coverage(taper,mesh.token) &&
          gpu.pick(width/2,height/2)==mesh.token,"Generated taper did not render or pick after atlas replacement.");
    std::cout<<"Native cylinder geometry, generated GLB atlas replacement and object picking passed\n";
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
