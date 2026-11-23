#include "renderer.hpp"
#include "vulkan_device.hpp"
#include "scene_spv.hpp"
#include <DirectXMath.h>
#include <algorithm>
#include <bit>
#include <utility>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace platz::engine {
using namespace DirectX;
using namespace gpu;
namespace {
using Clock = std::chrono::steady_clock;
constexpr float fov = XMConvertToRadians(perspectiveFovDegrees);
XMVECTOR vector(Vec3 v) {
    return XMVectorSet(v[0], v[1], v[2], 0);
}
XMVECTOR offset(const Camera& camera) {
    float cp = std::cos(camera.pitch);
    return XMVectorSet(std::sin(camera.yaw) * cp, std::sin(camera.pitch), std::cos(camera.yaw) * cp, 0);
}

// View-space sphere tests use the same right-handed view and FOV as the scene shader.
struct CameraFrame {
    XMVECTOR eye;
    XMMATRIX view, viewProjection;
    float nearZ, farZ, slopeX, slopeY, sideX, sideY;

    CameraFrame(const Camera& camera,float aspect) {
        const auto target=vector(camera.target);
        eye=target+offset(camera)*camera.distance;
        view=XMMatrixLookAtRH(eye,target,XMVectorSet(0,1,0,0));
        nearZ=camera.nearPlane();
        farZ=camera.farPlane();
        viewProjection=view*XMMatrixPerspectiveFovRH(fov,aspect,nearZ,farZ);
        slopeY=std::tan(fov*.5f);
        slopeX=slopeY*aspect;
        sideX=std::hypot(1.f,slopeX);
        sideY=std::hypot(1.f,slopeY);
    }
    bool outside(const DrawObject& object) const {
        // Enclose even an off-origin imported mesh under any rotation and nonuniform scale.
        auto half=XMVectorReplicate(.5f);
        if (object.asset)
            half=XMVectorMax(XMVectorAbs(vector(object.asset->minCorner())),XMVectorAbs(vector(object.asset->maxCorner())));
        const float radius=XMVectorGetX(XMVector3Length(half*vector(object.scale)));
        XMFLOAT3 center;
        XMStoreFloat3(&center,XMVector3TransformCoord(vector(object.position),view));
        const float depth=-center.z;
        const float margin=radius+.0001f+std::abs(depth)*.00001f;
        return depth+margin<nearZ || depth-margin>farZ ||
               std::abs(center.x)>depth*slopeX+margin*sideX ||
               std::abs(center.y)>depth*slopeY+margin*sideY;
    }
};

struct Vertex {
    XMFLOAT3 position, normal;
    XMFLOAT2 uv;
};

struct MeshRange {
    UINT startIndex, count;
};
struct ImportedRange {
    MeshRange range;
    std::uint32_t slot;
    bool hasUv;
};
struct SceneDraw {
    const DrawObject* object;
    MeshRange range;
    Vec3 color;
    VkDescriptorSet texture;
};
struct Geometry {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<MeshRange, 4> ranges{};
    UINT vertex(XMFLOAT3 p, XMFLOAT3 n, XMFLOAT2 uv = {}) {
        vertices.push_back({p, n, uv});
        return static_cast<UINT>(vertices.size() - 1);
    }
    void quad(UINT a, UINT b, UINT c, UINT d) {
        indices.insert(indices.end(), {a, b, c, a, c, d});
    }
    void finish(unsigned slot, UINT start) {
        ranges[slot] = {start, static_cast<UINT>(indices.size()) - start};
    }
};

Geometry makeGeometry() {
    Geometry g;
    // Separate face normals give the unit cube sharp edges under nonuniform scale.
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (float sign : {-1.f, 1.f}) {
            UINT first = static_cast<UINT>(g.vertices.size());
            for (auto corner : std::array<std::array<float, 2>, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}) {
                Vec3 p{}, n{};
                p[axis] = sign * .5f;
                n[axis] = sign;
                p[(axis + 1) % 3] = corner[0] * .5f;
                p[(axis + 2) % 3] = corner[1] * .5f;
                g.vertex({p[0], p[1], p[2]}, {n[0], n[1], n[2]});
            }
            g.quad(first, first + 1, first + 2, first + 3);
        }
    }
    g.finish(0, 0);
    constexpr unsigned rings = 32, segments = 64;
    UINT start = static_cast<UINT>(g.indices.size());
    UINT base = static_cast<UINT>(g.vertices.size());
    for (unsigned y = 0; y <= rings; ++y) {
        float theta = XM_PI * static_cast<float>(y) / rings;
        for (unsigned x = 0; x <= segments; ++x) {
            float phi = XM_2PI * static_cast<float>(x) / segments;
            XMFLOAT3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            g.vertex({n.x * .5f, n.y * .5f, n.z * .5f}, n);
            if (x && y) {
                UINT i = base + y * (segments + 1) + x;
                g.quad(i - segments - 2, i - 1, i, i - segments - 1);
            }
        }
    }
    g.finish(1, start);
    start = static_cast<UINT>(g.indices.size());
    for (unsigned i = 0; i < segments; ++i) {
        float a = XM_2PI * i / segments, b = XM_2PI * (i + 1) / segments;
        XMFLOAT3 n1{std::cos(a), 0, std::sin(a)}, n2{std::cos(b), 0, std::sin(b)};
        XMFLOAT3 bottom1{n1.x * .5f, -.5f, n1.z * .5f}, top1{bottom1.x, .5f, bottom1.z};
        XMFLOAT3 bottom2{n2.x * .5f, -.5f, n2.z * .5f}, top2{bottom2.x, .5f, bottom2.z};
        UINT p1 = g.vertex(bottom1, n1), p2 = g.vertex(top1, n1), p3 = g.vertex(top2, n2),
             p4 = g.vertex(bottom2, n2);
        g.quad(p1, p2, p3, p4);
        for (float y : {-.5f, .5f}) {
            XMFLOAT3 n{0, y * 2, 0};
            UINT center = g.vertex({0, y, 0}, n);
            UINT edge1 = g.vertex({bottom1.x, y, bottom1.z}, n), edge2 = g.vertex({bottom2.x, y, bottom2.z}, n);
            g.indices.insert(g.indices.end(), {center, edge1, edge2});
        }
    }
    g.finish(2, start);
    start = static_cast<UINT>(g.indices.size());
    base = static_cast<UINT>(g.vertices.size());
    for (auto p : std::array<XMFLOAT3, 4>{{{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}}})
        g.vertex(p, {0, 1, 0});
    g.quad(base, base + 1, base + 2, base + 3);
    g.finish(3, start);
    return g;
}

struct Constants {
    XMFLOAT4X4 viewProjection, world, normal;
    Vec3 color;
    std::uint32_t token;
    XMFLOAT3 eye;
    std::uint32_t selected;
    std::uint32_t mesh;
};
static_assert(sizeof(Constants) == 57 * 4);

} // namespace
void Camera::orbit(float dx, float dy) {
    yaw = std::remainder(yaw - dx * .006f, XM_2PI);
    pitch = std::clamp(pitch + dy * .006f, -1.50f, 1.50f);
}
void Camera::zoom(float steps) {
    distance = std::clamp(distance * std::exp(-steps * .14f), .002f, 2.e10f);
}
float Camera::nearPlane() const { return std::max(.00001f,distance*.001f); }
float Camera::farPlane() const { return std::max(2000.f,distance*5.f); }
void Camera::move(float right, float up, float forward) {
    target[0] += std::cos(yaw) * right - std::sin(yaw) * forward;
    target[1] += up;
    target[2] += -std::sin(yaw) * right - std::cos(yaw) * forward;
}
void Camera::pan(float dx, float dy, float height) {
    float units = 2 * distance * std::tan(fov / 2) / std::max(height, 1.f);
    move(-dx * units, dy * units * std::cos(pitch), dy * units * std::sin(pitch));
}
void Camera::frame(Vec3 center, Vec3 dimensions) {
    target = center;
    distance = std::clamp(XMVectorGetX(XMVector3Length(vector(dimensions))) * 1.45f, .5f, 650.f);
}
void Camera::frameObject(const DrawObject& object,float aspect) {
    if (!object.asset) { frame(object.position,object.scale); return; }
    const auto lo=vector(object.asset->minCorner()), hi=vector(object.asset->maxCorner());
    auto rotation=XMMatrixRotationX(XMConvertToRadians(object.rotation[0])) *
                  XMMatrixRotationY(XMConvertToRadians(object.rotation[1])) *
                  XMMatrixRotationZ(XMConvertToRadians(object.rotation[2]));
    auto localCenter=(lo+hi)*0.5f*vector(object.scale);
    XMFLOAT3 center;
    XMStoreFloat3(&center,XMVector3TransformNormal(localCenter,rotation)+vector(object.position));
    target={center.x,center.y,center.z};
    float radius=XMVectorGetX(XMVector3Length((hi-lo)*0.5f*vector(object.scale)));
    float halfAngle=std::atan(std::tan(fov/2)*std::clamp(aspect,0.02f,1.f));
    distance=std::clamp(1.15f*radius/std::sin(halfAngle),0.002f,2.e10f);
}

namespace {
constexpr uint32_t framesInFlight = 2;
constexpr VkFormat idFormat = VK_FORMAT_R32_UINT, depthFormat = VK_FORMAT_D32_SFLOAT;
constexpr uint64_t timeout = 10'000'000'000ull;

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                  VkImageLayout newLayout, VkPipelineStageFlags2 source, VkAccessFlags2 reads,
                  VkPipelineStageFlags2 destination, VkAccessFlags2 writes,
                  uint32_t firstMip=0,uint32_t mipCount=1) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = source;
    b.srcAccessMask = reads;
    b.dstStageMask = destination;
    b.dstAccessMask = writes;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, firstMip, mipCount, 0, 1};
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1;
    d.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &d);
}
// Derive each smaller sRGB level on the graphics queue before publishing the texture.
void generateTextureMips(VkCommandBuffer cmd,VkImage image,VkExtent2D extent,uint32_t levels) {
    constexpr auto aspect=VK_IMAGE_ASPECT_COLOR_BIT;
    constexpr auto source=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    constexpr auto destination=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    for (uint32_t next=1;next<levels;++next) {
        imageBarrier(cmd,image,aspect,destination,source,
            next==1 ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_BLIT_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_BLIT_BIT,VK_ACCESS_2_TRANSFER_READ_BIT,next-1);
        const VkExtent2D half{std::max(1u,extent.width/2),std::max(1u,extent.height/2)};
        VkImageBlit blit{};
        blit.srcSubresource={aspect,next-1,0,1};
        blit.dstSubresource={aspect,next,0,1};
        blit.srcOffsets[1]={int32_t(extent.width),int32_t(extent.height),1};
        blit.dstOffsets[1]={int32_t(half.width),int32_t(half.height),1};
        vkCmdBlitImage(cmd,image,source,image,destination,1,&blit,VK_FILTER_LINEAR);
        extent=half;
    }
    if (levels>1)
        imageBarrier(cmd,image,aspect,source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT|VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,0,levels-1);
    imageBarrier(cmd,image,aspect,destination,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        levels==1 ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_BLIT_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,levels-1);
}

void startCommands(VkDevice device, VkCommandPool pool, VkCommandBuffer cmd) {
    check(vkResetCommandPool(device, pool, 0), "Recycling completed frame commands");
    VkCommandBufferBeginInfo start{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    start.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd, &start), "Recording Vulkan commands");
}
void makeCommands(Device& d, VkCommandPool& pool, VkCommandBuffer& cmd, VkFence& fence) {
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pc.queueFamilyIndex = d.family;
    pc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    check(vkCreateCommandPool(d.device, &pc, nullptr, &pool), "Creating command pool");
    VkCommandBufferAllocateInfo ac{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ac.commandPool = pool;
    ac.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(d.device, &ac, &cmd), "Allocating command buffer");
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(d.device, &fc, nullptr, &fence), "Creating completion fence");
}
struct Frame {
    VkCommandPool pool{};
    VkCommandBuffer cmd{};
    VkFence fence{};
    VkSemaphore acquired{};
    VkDescriptorSet constants{};
    VkQueryPool timing{};
    Buffer uniforms;
    Buffer overlayVertices;
    uint32_t overlayCount{};
    Image color, ids, depth;
    bool rendered{};
};
struct PresentImage {
    VkImage image{};
    VkSemaphore finished{};
};
using TextureKey = std::pair<std::string,platz::BaseColorTexture>;
struct TextureInput {
    const platz::MeshAsset* embedded{};
    const platz::RasterImage* raster{};
};
using TextureSources = std::map<TextureKey,TextureInput>;
auto materialTexture(const DrawObject& object,unsigned slot) -> std::optional<std::pair<TextureKey,TextureInput>> {
    if (const auto* image=platz::slotImage(object.images,slot))
        return {{TextureKey{"rgba:"+image->raster->digest(),image->raster->sampling()},{nullptr,image->raster.get()}}};
    if (const auto* texture=object.asset->textureForSlot(slot,object.texturesOff))
        return {{TextureKey{object.asset->digest(),*texture},{object.asset.get(),nullptr}}};
    return {};
}
struct TextureBinding {
    Image image;
    VkSampler sampler{};
    VkDescriptorSet set{};
};
struct TextureBatch {
    VkDevice device{};
    VkDescriptorPool pool{};
    TextureBinding white;
    std::map<TextureKey,TextureBinding> entries;
    unsigned warnings{}, mipmapped{};
    ~TextureBatch() {
        if (pool) vkDestroyDescriptorPool(device,pool,nullptr);
        if (white.sampler) vkDestroySampler(device,white.sampler,nullptr);
        for (auto& [key,binding]:entries)
            if (binding.sampler) vkDestroySampler(device,binding.sampler,nullptr);
    }
};
} // namespace
struct Renderer::Impl {
    Device d; // Outlives every buffer, image and in-flight frame.
    std::array<Frame, framesInFlight> frames;
    uint32_t current{}, last{}, frameNumber{};
    VkDescriptorSetLayout binding{};
    VkDescriptorSetLayout textureLayout{};
    VkDescriptorPool descriptors{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{}, ground{}, overlayPipeline{}, occludedOverlay{};
    VkCommandPool transferPool{};
    VkCommandBuffer transferCmd{};
    VkFence transferFence{};
    VkCommandPool pickPool{};
    VkCommandBuffer pickCmd{};
    VkFence pickFence{};
    bool pickPending{};
    Buffer geometry, readback, pickReadback;
    Buffer pendingGeometry, geometryStaging;
    Buffer textureStaging;
    std::unique_ptr<TextureBatch> textures, pendingTextures;
    VkDeviceSize indexOffset{}, stride{};
    std::array<MeshRange, 4> ranges{};
    std::map<std::string,std::vector<ImportedRange>> imported;
    VkSwapchainKHR chain{};
    std::vector<PresentImage> images;
    VkExtent2D extent{}, requested{};
    VkFormat format{};
    Presentation presentation{Presentation::Fifo};
    VkPresentModeKHR presentMode{VK_PRESENT_MODE_FIFO_KHR};
    uint32_t activeFrames{framesInFlight};
    bool pendingResize{}, drawn{};
    double cpuMs{}, gpuMs{}, pipelineMs{};
    VkDeviceSize attachmentBytes{}, peakAttachmentBytes{}, maxViewportBytes{};
    void open(HWND);
    void rebuild();
    void releaseSwap();
    void makePipelines();
    void upload(const std::map<std::string,const platz::MeshAsset*>& assets);
    void uploadTextures(const TextureSources& sources);
    void finishTransfer();
    std::vector<SceneDraw> drawList;
    size_t lastCulled{};
    void prepare(Frame&, std::span<const SceneDraw>, const Camera&, const CameraFrame&, uint32_t);
    void record(Frame&, std::span<const SceneDraw>, uint32_t);
    bool draw(std::span<const DrawObject>, const Camera&, uint32_t, std::span<const OverlayVertex>, bool);
    Capture read(uint32_t, uint32_t, uint32_t, uint32_t, bool);
    ~Impl();
};

void Renderer::Impl::open(HWND hwnd) {
    d.open(hwnd);
    const auto alignment = d.properties.limits.minUniformBufferOffsetAlignment;
    stride = (shaders::sceneSize + alignment - 1) / alignment * alignment;
    VkDescriptorSetLayoutBinding uniform{};
    uniform.binding = shaders::sceneBinding;
    uniform.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    uniform.descriptorCount = 1;
    uniform.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo bc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    bc.bindingCount = 1;
    bc.pBindings = &uniform;
    check(vkCreateDescriptorSetLayout(d.device, &bc, nullptr, &binding), "Creating scene parameter binding");
    static_assert(shaders::sceneSet==0 && shaders::textureSet==1);
    VkFormatProperties textureFormat{};
    vkGetPhysicalDeviceFormatProperties(d.physical,VK_FORMAT_R8G8B8A8_SRGB,&textureFormat);
    constexpr VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((textureFormat.optimalTilingFeatures & required)!=required)
        throw std::runtime_error("This Vulkan device cannot sample sRGB material textures.");
    const std::array textureBindings{
        VkDescriptorSetLayoutBinding{shaders::imageBinding,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT},
        VkDescriptorSetLayoutBinding{shaders::samplerBinding,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT}};
    bc.bindingCount=static_cast<uint32_t>(textureBindings.size());
    bc.pBindings=textureBindings.data();
    check(vkCreateDescriptorSetLayout(d.device,&bc,nullptr,&textureLayout),"Creating material texture layout");
    const std::array layouts{binding,textureLayout};
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = static_cast<uint32_t>(layouts.size());
    lc.pSetLayouts = layouts.data();
    const VkPushConstantRange overlayStyle{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(float)};
    lc.pushConstantRangeCount=1;
    lc.pPushConstantRanges=&overlayStyle;
    check(vkCreatePipelineLayout(d.device, &lc, nullptr, &layout), "Creating material parameter layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, framesInFlight};
    VkDescriptorPoolCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dc.maxSets = framesInFlight;
    dc.poolSizeCount = 1;
    dc.pPoolSizes = &size;
    check(vkCreateDescriptorPool(d.device, &dc, nullptr, &descriptors), "Creating frame descriptors");
    VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& f : frames) {
        makeCommands(d, f.pool, f.cmd, f.fence);
        check(vkCreateSemaphore(d.device, &sc, nullptr, &f.acquired), "Creating image-acquired semaphore");
        VkDescriptorSetAllocateInfo ac{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ac.descriptorPool = descriptors;
        ac.descriptorSetCount = 1;
        ac.pSetLayouts = &binding;
        check(vkAllocateDescriptorSets(d.device, &ac, &f.constants), "Allocating frame parameter set");
        if (d.timestampBits) {
            VkQueryPoolCreateInfo qc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qc.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qc.queryCount = 2;
            check(vkCreateQueryPool(d.device, &qc, nullptr, &f.timing), "Allocating GPU timestamps");
        }
    }
    makeCommands(d, transferPool, transferCmd, transferFence);
    makeCommands(d, pickPool, pickCmd, pickFence);
    pickReadback=d.buffer(sizeof(uint32_t),VK_BUFFER_USAGE_TRANSFER_DST_BIT,MemoryIntent::Readback,"Selection pixel");
    upload({});
    RECT rect{};
    GetClientRect(hwnd, &rect);
    requested = {static_cast<uint32_t>(std::max<LONG>(1, rect.right)),
                 static_cast<uint32_t>(std::max<LONG>(1, rect.bottom))};
    rebuild();
}
Renderer::Impl::~Impl() {
    if (!d.device)
        return;
    vkDeviceWaitIdle(d.device);
    releaseSwap();
    for (auto& f : frames) {
        if (f.timing)
            vkDestroyQueryPool(d.device, f.timing, nullptr);
        if (f.acquired)
            vkDestroySemaphore(d.device, f.acquired, nullptr);
        if (f.fence)
            vkDestroyFence(d.device, f.fence, nullptr);
        if (f.pool)
            vkDestroyCommandPool(d.device, f.pool, nullptr);
    }
    if (transferFence)
        vkDestroyFence(d.device, transferFence, nullptr);
    if (transferPool)
        vkDestroyCommandPool(d.device, transferPool, nullptr);
    if (pickFence)
        vkDestroyFence(d.device,pickFence,nullptr);
    if (pickPool)
        vkDestroyCommandPool(d.device,pickPool,nullptr);
    if (pipeline)
        vkDestroyPipeline(d.device, pipeline, nullptr);
    if (ground)
        vkDestroyPipeline(d.device, ground, nullptr);
    if (overlayPipeline)
        vkDestroyPipeline(d.device, overlayPipeline, nullptr);
    if (occludedOverlay)
        vkDestroyPipeline(d.device,occludedOverlay,nullptr);
    if (layout)
        vkDestroyPipelineLayout(d.device, layout, nullptr);
    textures.reset();
    pendingTextures.reset();
    if (textureLayout)
        vkDestroyDescriptorSetLayout(d.device,textureLayout,nullptr);
    if (descriptors)
        vkDestroyDescriptorPool(d.device, descriptors, nullptr);
    if (binding)
        vkDestroyDescriptorSetLayout(d.device, binding, nullptr);
}
void submitTransfer(const Device& d,VkCommandBuffer commands,VkFence fence) {
    check(vkEndCommandBuffer(commands), "Closing transfer commands");
    VkCommandBufferSubmitInfo cmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd.commandBuffer = commands;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd;
    check(vkResetFences(d.device, 1, &fence), "Resetting transfer completion");
    check(vkQueueSubmit2(d.queue, 1, &submit, fence), "Submitting GPU transfer");
}
void Renderer::Impl::finishTransfer() {
    submitTransfer(d,transferCmd,transferFence);
    check(vkWaitForFences(d.device, 1, &transferFence, VK_TRUE, timeout), "Waiting for GPU transfer");
}
void Renderer::Impl::upload(const std::map<std::string,const platz::MeshAsset*>& assets) {
    auto source = makeGeometry();
    std::map<std::string,std::vector<ImportedRange>> newRanges;
    for(const auto& [digest,asset]:assets) {
        const auto base=static_cast<uint32_t>(source.vertices.size());
        auto& meshRanges=newRanges[digest];
        const auto firstIndex=static_cast<UINT>(source.indices.size());
        for (const auto& primitive:asset->primitives())
            meshRanges.push_back({{firstIndex+primitive.firstIndex,primitive.indexCount},primitive.slot,primitive.hasUv});
        for(const auto& v:asset->vertices()) source.vertex(
            {v.position[0],v.position[1],v.position[2]}, {v.normal[0],v.normal[1],v.normal[2]}, {v.uv[0],v.uv[1]});
        for(auto index:asset->indices()) source.indices.push_back(base+index);
    }
    const VkDeviceSize vertexBytes = source.vertices.size() * sizeof(Vertex);
    VkDeviceSize bytes = vertexBytes + source.indices.size() * sizeof(uint32_t);
    auto replacement = d.buffer(bytes,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        MemoryIntent::Mesh, "Scene mesh atlas");
    auto staging = d.buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryIntent::Upload, "Mesh upload");
    std::memcpy(staging.mapped, source.vertices.data(), vertexBytes);
    std::memcpy(static_cast<std::byte*>(staging.mapped) + vertexBytes, source.indices.data(),
                bytes - vertexBytes);
    check(vmaFlushAllocation(d.allocator, staging.allocation, 0, VK_WHOLE_SIZE), "Flushing mesh upload");
    startCommands(d.device, transferPool, transferCmd);
    VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(transferCmd, staging.handle, replacement.handle, 1, &copy);
    VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    b.dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = replacement.handle;
    b.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &b;
    vkCmdPipelineBarrier2(transferCmd, &dep);
    // Keep both buffers alive if the submitted transfer times out.
    pendingGeometry=std::move(replacement);
    geometryStaging=std::move(staging);
    finishTransfer();
    // The transfer fence also retires preceding draws on this graphics queue.
    geometry=std::move(pendingGeometry);
    geometryStaging={};
    ranges=source.ranges;
    indexOffset=vertexBytes;
    imported=std::move(newRanges);
}
void Renderer::Impl::uploadTextures(const TextureSources& sources) {
    VkDeviceSize bytes=4;
    for (const auto& item:sources) bytes+=item.first.second.rgbaSize();
    if (sources.size()>BaseColorTexture::maxActive || bytes-4>BaseColorTexture::maxActiveBytes)
        throw std::runtime_error("Material textures exceed the viewport's texture budget.");
    auto batch=std::make_unique<TextureBatch>();
    batch->device=d.device;
    const auto capacity=static_cast<uint32_t>(sources.size()+1);
    const std::array sizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,capacity},
                           VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER,capacity}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=capacity;
    pool.poolSizeCount=static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes=sizes.data();
    check(vkCreateDescriptorPool(d.device,&pool,nullptr,&batch->pool),"Creating material descriptor pool");
    auto staging=d.buffer(bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,MemoryIntent::Upload,"Base-color texture upload");
    auto pixels=std::span(static_cast<std::uint8_t*>(staging.mapped),static_cast<size_t>(bytes));
    std::fill_n(pixels.begin(),4,std::uint8_t{255});
    startCommands(d.device,transferPool,transferCmd);
    constexpr auto textureFormat=VK_FORMAT_R8G8B8A8_SRGB;
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(d.physical,textureFormat,&format);
    constexpr auto mipFeatures=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|
                               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const bool canGenerateMips=(format.optimalTilingFeatures&mipFeatures)==mipFeatures;
    const auto makeBinding=[&](TextureBinding& target,const BaseColorTexture& asset,VkDeviceSize offset) {
        const VkExtent2D extent{asset.width,asset.height};
        const uint32_t levels=canGenerateMips && asset.mip!=TextureMipFilter::None
            ? std::bit_width(std::max(extent.width,extent.height)) : 1u;
        VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        if (levels>1) usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        target.image=d.image(extent,textureFormat,usage,VK_IMAGE_ASPECT_COLOR_BIT,"Base-color image",levels);
        imageBarrier(transferCmd,target.image.handle,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_NONE,0,
            VK_PIPELINE_STAGE_2_COPY_BIT|VK_PIPELINE_STAGE_2_BLIT_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,0,levels);
        VkBufferImageCopy copy{};
        copy.bufferOffset=offset;
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageExtent={extent.width,extent.height,1};
        vkCmdCopyBufferToImage(transferCmd,staging.handle,target.image.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        generateTextureMips(transferCmd,target.image.handle,extent,levels);
        if (levels>1) ++batch->mipmapped;

        const auto address=[](TextureWrap mode) {
            if (mode==TextureWrap::Clamp) return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            if (mode==TextureWrap::Mirror) return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        };
        const auto filter=[](TextureFilter mode) {
            return mode==TextureFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        };
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter=filter(asset.mag);
        sampler.minFilter=filter(asset.min);
        sampler.addressModeU=address(asset.wrapU);
        sampler.addressModeV=address(asset.wrapV);
        sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.mipmapMode=asset.mip==TextureMipFilter::Linear
            ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.maxLod=float(levels-1);
        check(vkCreateSampler(d.device,&sampler,nullptr,&target.sampler),"Creating base-color sampler");
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool=batch->pool;
        alloc.descriptorSetCount=1;
        alloc.pSetLayouts=&textureLayout;
        check(vkAllocateDescriptorSets(d.device,&alloc,&target.set),"Allocating material descriptor set");
        const VkDescriptorImageInfo image{VK_NULL_HANDLE,target.image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo samplerInfo{target.sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
        VkWriteDescriptorSet writes[2]{};
        for (auto& write:writes) {
            write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet=target.set;
            write.descriptorCount=1;
        }
        writes[0].dstBinding=shaders::imageBinding;
        writes[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].pImageInfo=&image;
        writes[1].dstBinding=shaders::samplerBinding;
        writes[1].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].pImageInfo=&samplerInfo;
        vkUpdateDescriptorSets(d.device,2,writes,0,nullptr);
    };
    BaseColorTexture white;
    white.width=white.height=1;
    makeBinding(batch->white,white,0);
    size_t offset=4;
    for (const auto& [key,source]:sources) {
        auto& target=batch->entries[key];
        const auto& asset=key.second;
        const auto start=offset;
        offset+=asset.rgbaSize();
        try {
            if (source.raster)
                std::ranges::copy(source.raster->pixels(),pixels.begin()+start);
            else source.embedded->readTexturePixels(asset,pixels.subspan(start,asset.rgbaSize()));
        }
        catch (const std::runtime_error&) {
            ++batch->warnings;
            target.set=batch->white.set;
            continue;
        }
        makeBinding(target,asset,start);
    }
    check(vmaFlushAllocation(d.allocator,staging.allocation,0,VK_WHOLE_SIZE),"Flushing texture upload");
    pendingTextures=std::move(batch);
    textureStaging=std::move(staging);
    finishTransfer();
    // Earlier frames using old descriptors have retired on this graphics queue.
    textures=std::move(pendingTextures);
    textureStaging={};
}
void Renderer::Impl::releaseSwap() {
    for (auto& image : images)
        if (image.finished)
            vkDestroySemaphore(d.device, image.finished, nullptr);
    images.clear();
    for (auto& f : frames) {
        f.color = {};
        f.ids = {};
        f.depth = {};
        f.rendered = false;
    }
    if (chain)
        vkDestroySwapchainKHR(d.device, chain, nullptr);
    chain = {};
    drawn = false;
    attachmentBytes = 0;
}
void Renderer::Impl::rebuild() {
    // Only resize/teardown drain the presentation queue. Normal frame reuse waits its own fence.
    check(vkQueueWaitIdle(d.queue), "Retiring the previous viewport");
    releaseSwap();
    pendingResize = false;
    if (!requested.width || !requested.height)
        return;
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d.physical, d.surface, &caps), "Reading surface extent");
    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (!extent.width || !extent.height) {
        pendingResize = true;
        return;
    }
    uint32_t count{};
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(d.physical, d.surface, &count, nullptr),
          "Counting surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(d.physical, d.surface, &count, formats.data()),
          "Reading surface formats");
    auto compatible = std::find_if(formats.begin(), formats.end(), [&](auto s) {
        if (s.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
            (s.format != VK_FORMAT_B8G8R8A8_UNORM && s.format != VK_FORMAT_R8G8B8A8_UNORM))
            return false;
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(d.physical, s.format, &p);
        auto flags = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        return (p.optimalTilingFeatures & flags) == flags;
    });
    if (compatible == formats.end())
        throw std::runtime_error("This display lacks a qualified 8-bit SDR presentation format.");
    const bool changed = format != compatible->format;
    format = compatible->format;
    presentMode=VK_PRESENT_MODE_FIFO_KHR;
    if (presentation==Presentation::Interactive) {
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(d.physical,d.surface,&count,nullptr),"Counting present modes");
        std::vector<VkPresentModeKHR> modes(count);
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(d.physical,d.surface,&count,modes.data()),"Reading present modes");
        if (std::ranges::find(modes,VK_PRESENT_MODE_MAILBOX_KHR)!=modes.end()) presentMode=VK_PRESENT_MODE_MAILBOX_KHR;
    }
    const bool fifoFallback=presentation==Presentation::Interactive && presentMode==VK_PRESENT_MODE_FIFO_KHR;
    activeFrames=fifoFallback ? 1 : framesInFlight;
    current=0;
    uint32_t imageCount = fifoFallback ? caps.minImageCount : std::max(caps.minImageCount + 1, framesInFlight);
    if (caps.maxImageCount)
        imageCount = std::min(imageCount, caps.maxImageCount);
    const uint64_t pixels = uint64_t(extent.width) * extent.height;
    const auto budget=std::min(d.viewportBudget(),maxViewportBytes);
    if (pixels * (activeFrames * 12ull + imageCount * 4ull) > budget)
        throw std::runtime_error(
            "The viewport exceeds its graphics-memory budget. Reduce the window size or free GPU memory.");
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = d.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = format;
    ci.imageColorSpace = compatible->colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha &
                                                                 (~caps.supportedCompositeAlpha + 1u));
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(d.device, &ci, nullptr, &chain), "Creating viewport swapchain");
    check(vkGetSwapchainImagesKHR(d.device, chain, &count, nullptr), "Counting presentation images");
    if (pixels * (activeFrames * 12ull + count * 4ull) > budget)
        throw std::runtime_error("The display's swapchain exceeds the viewport's graphics-memory budget.");
    std::vector<VkImage> native(count);
    check(vkGetSwapchainImagesKHR(d.device, chain, &count, native.data()), "Reading presentation images");
    images.resize(count);
    VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < count; ++i) {
        images[i].image = native[i];
        check(vkCreateSemaphore(d.device, &sc, nullptr, &images[i].finished),
              "Creating per-image presentation semaphore");
    }
    for (auto& f : std::span(frames).first(activeFrames)) {
        const auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        f.color = d.image(extent, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, "Viewport display color");
        f.ids = d.image(extent, idFormat, usage, VK_IMAGE_ASPECT_COLOR_BIT, "Scene identity attachment");
        f.depth = d.image(extent, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT, "Scene depth");
    }
    attachmentBytes = pixels * activeFrames * 12ull;
    peakAttachmentBytes = std::max(attachmentBytes, peakAttachmentBytes);
    if (changed || !pipeline)
        makePipelines();
}

void Renderer::Impl::makePipelines() {
    const auto begin = Clock::now();
    if (pipeline)
        vkDestroyPipeline(d.device, std::exchange(pipeline, VK_NULL_HANDLE), nullptr);
    if (ground)
        vkDestroyPipeline(d.device, std::exchange(ground, VK_NULL_HANDLE), nullptr);
    if (overlayPipeline)
        vkDestroyPipeline(d.device, std::exchange(overlayPipeline, VK_NULL_HANDLE), nullptr);
    if (occludedOverlay)
        vkDestroyPipeline(d.device,std::exchange(occludedOverlay,VK_NULL_HANDLE),nullptr);
    auto makeModule = [&](std::span<const uint32_t> code) {
        VkShaderModule module{};
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size_bytes();
        ci.pCode = code.data();
        check(vkCreateShaderModule(d.device, &ci, nullptr, &module), "Loading validated SPIR-V");
        return module;
    };
    struct Modules {
        VkDevice device;
        VkShaderModule vs{}, fs{};
        ~Modules() {
            if (vs)
                vkDestroyShaderModule(device, vs, nullptr);
            if (fs)
                vkDestroyShaderModule(device, fs, nullptr);
        }
    } modules{d.device};
    modules.vs = makeModule(shaders::vertex);
    modules.fs = makeModule(shaders::fragment);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = modules.vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = modules.fs;
    stages[1].pName = "main";
    VkVertexInputBindingDescription stream{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription inputs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                  {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,normal)},
                                                  {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex,uv)}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &stream;
    vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(inputs));
    vertex.pVertexAttributeDescriptions = inputs;
    VkPipelineInputAssemblyStateCreateInfo topology{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    topology.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.lineWidth = 1;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthTest{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthTest.depthTestEnable = VK_TRUE;
    depthTest.depthWriteEnable = VK_TRUE;
    depthTest.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState output[2]{};
    output[0].colorWriteMask = output[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 2;
    blend.pAttachments = output;
    VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkFormat formats[] = {format, idFormat};
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 2;
    rendering.pColorAttachmentFormats = formats;
    rendering.depthAttachmentFormat = depthFormat;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pInputAssemblyState = &topology;
    ci.pVertexInputState = &vertex;
    ci.pViewportState = &viewport;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState = &samples;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dynamic;
    ci.pDepthStencilState = &depthTest;
    ci.layout = layout;
    check(vkCreateGraphicsPipelines(d.device, {}, 1, &ci, nullptr, &pipeline),
          "Creating opaque geometry pipeline");
    depthTest.depthWriteEnable = VK_FALSE;
    check(vkCreateGraphicsPipelines(d.device, {}, 1, &ci, nullptr, &ground),
          "Creating construction grid pipeline");
    Modules overlayModules{d.device};
    overlayModules.vs=makeModule(shaders::overlayVertex);
    overlayModules.fs=makeModule(shaders::overlayFragment);
    stages[0].module=overlayModules.vs;
    stages[1].module=overlayModules.fs;
    stream.stride=sizeof(OverlayVertex);
    const VkVertexInputAttributeDescription overlayInputs[]={
        {0,0,VK_FORMAT_R32G32_SFLOAT,offsetof(OverlayVertex,position)},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(OverlayVertex,color)},
        {2,0,VK_FORMAT_R32G32_SFLOAT,offsetof(OverlayVertex,depth)}};
    static_assert(offsetof(OverlayVertex,opacity)==offsetof(OverlayVertex,depth)+sizeof(float));
    vertex.vertexAttributeDescriptionCount=static_cast<uint32_t>(std::size(overlayInputs));
    vertex.pVertexAttributeDescriptions=overlayInputs;
    rendering.colorAttachmentCount=1;
    blend.attachmentCount=1;
    output[0].blendEnable=VK_TRUE;
    output[0].srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
    output[0].dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    output[0].colorBlendOp=VK_BLEND_OP_ADD;
    output[0].srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
    output[0].dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    output[0].alphaBlendOp=VK_BLEND_OP_ADD;
    depthTest.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    check(vkCreateGraphicsPipelines(d.device,{},1,&ci,nullptr,&overlayPipeline),"Creating viewport overlay");
    depthTest.depthCompareOp=VK_COMPARE_OP_GREATER;
    check(vkCreateGraphicsPipelines(d.device,{},1,&ci,nullptr,&occludedOverlay),"Creating occluded overlay");
    d.label(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline), "Opaque meshes and identity");
    pipelineMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

void Renderer::Impl::prepare(Frame& f, std::span<const SceneDraw> draws, const Camera& camera,
                             const CameraFrame& frame,uint32_t selected) {
    static_assert(offsetof(Constants, viewProjection) == shaders::offset::viewProjection &&
                  offsetof(Constants, world) == shaders::offset::world);
    static_assert(offsetof(Constants, normal) == shaders::offset::normalMatrix &&
                  offsetof(Constants, color) == shaders::offset::baseColor);
    static_assert(offsetof(Constants, eye) == shaders::offset::eye &&
                  offsetof(Constants, selected) == shaders::offset::selected);
    static_assert(offsetof(Constants, token) == shaders::offset::token &&
                  offsetof(Constants, mesh) == shaders::offset::meshKind);
    static_assert(sizeof(Constants) <= shaders::sceneSize && shaders::sceneSet == 0);
    auto slots = draws.size() + 1;
    if (slots > std::min<VkDeviceSize>(64ull << 20, UINT32_MAX) / stride)
        throw std::runtime_error("Scene draw parameters exceed the per-frame resource budget.");
    const auto used = slots * stride;
    if (used > f.uniforms.size) {
        f.uniforms = d.buffer(std::max<VkDeviceSize>(used, 32 * stride), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              MemoryIntent::Upload, "Frame draw parameters");
        VkDescriptorBufferInfo info{f.uniforms.handle, 0, shaders::sceneSize};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = f.constants;
        write.dstBinding = shaders::sceneBinding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(d.device, 1, &write, 0, nullptr);
    }
    Constants c{};
    XMStoreFloat4x4(&c.viewProjection,frame.viewProjection);
    XMStoreFloat3(&c.eye,frame.eye);
    c.selected = selected;
    auto transform = [&](XMMATRIX world) {
        XMStoreFloat4x4(&c.world, world);
        XMStoreFloat4x4(&c.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
    };
    auto store = [&](size_t i) {
        std::memcpy(static_cast<std::byte*>(f.uniforms.mapped) + i * stride, &c, sizeof(c));
    };
    c.mesh = 3;
    transform(XMMatrixScaling(1800, 1, 1800) * XMMatrixTranslation(camera.target[0], -.012f, camera.target[2]));
    store(0);
    const DrawObject* previous=nullptr;
    for (size_t i = 0; i < draws.size(); ++i) {
        const auto& o = *draws[i].object;
        if (&o!=previous) {
            const auto rotation = XMMatrixRotationX(XMConvertToRadians(o.rotation[0])) *
                                  XMMatrixRotationY(XMConvertToRadians(o.rotation[1])) *
                                  XMMatrixRotationZ(XMConvertToRadians(o.rotation[2]));
            transform(XMMatrixScalingFromVector(vector(o.scale)) * rotation *
                      XMMatrixTranslationFromVector(vector(o.position)));
            c.mesh = static_cast<uint32_t>(o.mesh);
            c.token = o.token;
            previous=&o;
        }
        c.color = draws[i].color;
        store(i + 1);
    }
    check(vmaFlushAllocation(d.allocator, f.uniforms.allocation, 0, used), "Flushing scene parameters");
}

void Renderer::Impl::record(Frame& f, std::span<const SceneDraw> draws, uint32_t imageIndex) {
    auto cmd = f.cmd;
    startCommands(d.device, f.pool, cmd);
    if (f.timing) {
        vkCmdResetQueryPool(cmd, f.timing, 0, 2);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, f.timing, 0);
    }
    for (auto* image : {&f.color, &f.ids}) {
        imageBarrier(cmd, image->handle, image->aspect,
                     f.rendered ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     f.rendered ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_NONE,
                     f.rendered ? VK_ACCESS_2_TRANSFER_READ_BIT : 0,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    constexpr auto tests =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    imageBarrier(
        cmd, f.depth.handle, f.depth.aspect,
        f.rendered ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, f.rendered ? tests : VK_PIPELINE_STAGE_2_NONE,
        f.rendered ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0, tests,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    auto attachment = [](VkImageView view, VkImageLayout layout) {
        VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = view;
        a.imageLayout = layout;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        return a;
    };
    VkRenderingAttachmentInfo colors[] = {attachment(f.color.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
                                          attachment(f.ids.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)};
    colors[0].clearValue.color = {{.100f, .126f, .155f, 1.f}};
    auto dep = attachment(f.depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    dep.clearValue.depthStencil = {1, 0};
    dep.storeOp = f.overlayCount ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkRenderingInfo render{VK_STRUCTURE_TYPE_RENDERING_INFO};
    render.renderArea = {{0, 0}, extent};
    render.layerCount = 1;
    render.colorAttachmentCount = 2;
    render.pColorAttachments = colors;
    render.pDepthAttachment = &dep;
    vkCmdBeginRendering(cmd, &render);
    VkViewport viewport{0, float(extent.height), float(extent.width), -float(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkDeviceSize zero{};
    vkCmdBindVertexBuffers(cmd, 0, 1, &geometry.handle, &zero);
    vkCmdBindIndexBuffer(cmd, geometry.handle, indexOffset, VK_INDEX_TYPE_UINT32);
    VkDescriptorSet boundTexture{};
    auto submit = [&](size_t i, MeshRange range, VkDescriptorSet texture) {
        uint32_t off = static_cast<uint32_t>(stride * i);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, shaders::sceneSet, 1,
                                &f.constants, 1, &off);
        if (texture!=boundTexture) {
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,shaders::textureSet,1,
                                    &texture,0,nullptr);
            boundTexture=texture;
        }
        vkCmdDrawIndexed(cmd, range.count, 1, range.startIndex, 0, 0);
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ground);
    submit(0, ranges[3],textures->white.set);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    for (size_t i = 0; i < draws.size(); ++i)
        submit(i+1,draws[i].range,draws[i].texture);
    vkCmdEndRendering(cmd);
    if (f.overlayCount) {
        // Preserve scene IDs while drawing visible and occluded handles against scene depth.
        imageBarrier(cmd,f.color.handle,f.color.aspect,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        colors[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
        imageBarrier(cmd,f.depth.handle,f.depth.aspect,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,tests,VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            tests,VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        dep.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
        dep.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        render.colorAttachmentCount=1;
        vkCmdBeginRendering(cmd,&render);
        vkCmdBindVertexBuffers(cmd,0,1,&f.overlayVertices.handle,&zero);
        for (const auto [pass,opacity] : {std::pair{occludedOverlay,.3f},std::pair{overlayPipeline,1.f}}) {
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pass);
            vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(opacity),&opacity);
            vkCmdDraw(cmd,f.overlayCount,1,0,0);
        }
        vkCmdEndRendering(cmd);
    }
    for (auto* image : {&f.color, &f.ids})
        imageBarrier(cmd, image->handle, image->aspect, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    auto back = images[imageIndex].image;
    imageBarrier(cmd, back, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(cmd, f.color.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, back,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    imageBarrier(cmd, back, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_NONE, 0);
    if (f.timing)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, f.timing, 1);
    check(vkEndCommandBuffer(cmd), "Closing viewport commands");
}
bool Renderer::Impl::draw(std::span<const DrawObject> objects, const Camera& camera, uint32_t selected,
                          std::span<const OverlayVertex> overlay, bool waitForFrame) {
    auto begin = Clock::now();
    if (pendingResize)
        rebuild();
    if (!chain)
        return false;
    auto& f = frames[current];
    const auto available=waitForFrame ? vkWaitForFences(d.device,1,&f.fence,VK_TRUE,timeout) :
                                       vkGetFenceStatus(d.device,f.fence);
    if (!waitForFrame && available==VK_NOT_READY) return false;
    check(available,"Reusing completed frame");
    std::map<std::string,const platz::MeshAsset*> assets;
    for(const auto& object:objects) if(object.asset) assets.try_emplace(object.asset->digest(),object.asset.get());
    if(assets.size()!=imported.size() || !std::equal(assets.begin(),assets.end(),imported.begin(),
            [](const auto& a,const auto& b){return a.first==b.first;})) upload(assets);
    TextureSources sources;
    for (const auto& object:objects) {
        if (!object.token || !object.asset) continue;
        for (std::uint32_t slot=0;slot<object.asset->materialSlots().size();++slot)
            if (const auto input=materialTexture(object,slot))
                sources.try_emplace(input->first,input->second);
    }
    if (!textures || sources.size()!=textures->entries.size() ||
        !std::equal(sources.begin(),sources.end(),textures->entries.begin(),
                    [](const auto& a,const auto& b){return a.first==b.first;})) uploadTextures(sources);
    const CameraFrame cameraFrame(camera,float(extent.width)/float(extent.height));
    size_t culled=0;
    auto& draws=drawList;
    draws.clear();
    draws.reserve(objects.size());
    for (const auto& object:objects) {
        if (!object.token) continue;
        if (cameraFrame.outside(object)) {
            ++culled;
            continue;
        }
        if (object.asset) {
            for (const auto& primitive:imported.at(object.asset->digest())) {
                const auto color=object.asset->colorForSlot(primitive.slot,object.color,object.slotColors);
                const auto texture=primitive.hasUv ? materialTexture(object,primitive.slot) : std::nullopt;
                const auto set=texture ? textures->entries.at(texture->first).set : textures->white.set;
                draws.push_back({&object,primitive.range,color,set});
            }
        } else if (const auto kind=static_cast<uint32_t>(object.mesh); kind<3) {
            draws.push_back({&object,ranges[kind],object.color,textures->white.set});
        }
    }
    if (f.rendered && f.timing) {
        uint64_t times[2]{};
        check(vkGetQueryPoolResults(d.device, f.timing, 0, 2, sizeof(times), times, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT),
              "Collecting GPU timing");
        const uint64_t mask = d.timestampBits == 64 ? UINT64_MAX : ((uint64_t{1} << d.timestampBits) - 1);
        gpuMs = double((times[1] - times[0]) & mask) * d.properties.limits.timestampPeriod / 1e6;
    }
    vmaSetCurrentFrameIndex(d.allocator, ++frameNumber);
    if (overlay.size()>UINT32_MAX || overlay.size()%3 || overlay.size_bytes()>maxViewportBytes/activeFrames)
        throw std::runtime_error("Viewport overlay has an invalid triangle count.");
    f.overlayCount=static_cast<uint32_t>(overlay.size());
    if (!overlay.empty()) {
        const auto bytes=overlay.size_bytes();
        if (f.overlayVertices.size<bytes)
            f.overlayVertices=d.buffer(std::max<uint64_t>(bytes,std::min(f.overlayVertices.size*2,maxViewportBytes/activeFrames)),
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                       MemoryIntent::Upload,"Gizmo vertices");
        std::memcpy(f.overlayVertices.mapped,overlay.data(),bytes);
        check(vmaFlushAllocation(d.allocator,f.overlayVertices.allocation,0,bytes),"Flushing viewport overlay");
    }
    prepare(f, draws, camera, cameraFrame, selected);
    uint32_t idx{};
    VkResult acquired = vkAcquireNextImageKHR(d.device, chain, waitForFrame ? timeout : 0, f.acquired, {}, &idx);
    if (!waitForFrame && (acquired==VK_NOT_READY || acquired==VK_TIMEOUT)) return false;
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        pendingResize = true;
        drawn = false;
        return false;
    }
    if (acquired != VK_SUBOPTIMAL_KHR)
        check(acquired, "Acquiring display image");
    pendingResize = acquired == VK_SUBOPTIMAL_KHR;
    record(f, draws, idx);
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = images[idx].finished;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commands.commandBuffer = f.cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = submit.signalSemaphoreInfoCount = submit.commandBufferInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.pSignalSemaphoreInfos = &signal;
    submit.pCommandBufferInfos = &commands;
    check(vkResetFences(d.device, 1, &f.fence), "Resetting frame completion");
    check(vkQueueSubmit2(d.queue, 1, &submit, f.fence), "Submitting scene");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.swapchainCount = 1;
    present.pSwapchains = &chain;
    present.pImageIndices = &idx;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &images[idx].finished;
    auto result = vkQueuePresentKHR(d.queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        pendingResize = true;
    else
        check(result, "Presenting scene");
    f.rendered = true;
    lastCulled=culled;
    last = current;
    current = (current + 1) % activeFrames;
    drawn = result != VK_ERROR_OUT_OF_DATE_KHR;
    cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    if (d.errors.load())
        throw std::runtime_error("Vulkan validation reported an error. See the validation log.");
    return drawn;
}

Capture Renderer::Impl::read(uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool withColor) {
    Capture result;
    if (!drawn || !w || !h)
        return result;
    VkDeviceSize plane = VkDeviceSize(w) * h * 4;
    const auto bytes = plane * (withColor ? 2 : 1);
    if (bytes > readback.size)
        readback =
            d.buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryIntent::Readback, "Viewport readback");
    auto& f = frames[last];
    startCommands(d.device, transferPool, transferCmd);
    auto copy = [&](VkImage image, VkDeviceSize offset) {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {static_cast<int>(x), static_cast<int>(y), 0};
        region.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(transferCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1,
                               &region);
    };
    copy(f.ids.handle, 0);
    if (withColor)
        copy(f.color.handle, plane);
    finishTransfer();
    check(vmaInvalidateAllocation(d.allocator, readback.allocation, 0, bytes), "Reading completed frame");
    result.width = w;
    result.height = h;
    result.tokens.resize(plane / 4);
    std::memcpy(result.tokens.data(), readback.mapped, plane);
    if (withColor) {
        result.rgba.resize(plane);
        std::memcpy(result.rgba.data(), static_cast<uint8_t*>(readback.mapped) + plane, plane);
        if (format == VK_FORMAT_B8G8R8A8_UNORM)
            for (size_t i = 0; i < result.rgba.size(); i += 4)
                std::swap(result.rgba[i], result.rgba[i + 2]);
    }
    return result;
}
Renderer::Renderer(void* window,uint64_t maxViewportBytes,Presentation presentation) : impl_(std::make_unique<Impl>()) {
    impl_->maxViewportBytes = maxViewportBytes;
    impl_->presentation = presentation;
    impl_->open(static_cast<HWND>(window));
}
Renderer::~Renderer() = default;
void Renderer::resize(unsigned w, unsigned h) {
    if (impl_->requested.width != w || impl_->requested.height != h) {
        impl_->requested = {w, h};
        impl_->pendingResize = true;
        impl_->drawn = false;
    }
}
bool Renderer::draw(std::span<const DrawObject> objects, const Camera& c, uint32_t selected,
                    std::span<const OverlayVertex> overlay,bool waitForFrame) {
    return impl_->draw(objects,c,selected,overlay,waitForFrame);
}
bool Renderer::requestPick(int x,int y) {
    auto& i=*impl_;
    if (i.pickPending || !i.drawn || x<0 || y<0 || static_cast<unsigned>(x)>=i.extent.width ||
        static_cast<unsigned>(y)>=i.extent.height) return false;
    startCommands(i.d.device,i.pickPool,i.pickCmd);
    VkBufferImageCopy region{};
    region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    region.imageOffset={x,y,0};
    region.imageExtent={1,1,1};
    // On the graphics queue, the frame's final barrier publishes the ID attachment to this copy.
    // The next use of that attachment in turn waits for transfer reads before writing.
    vkCmdCopyImageToBuffer(i.pickCmd,i.frames[i.last].ids.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           i.pickReadback.handle,1,&region);
    submitTransfer(i.d,i.pickCmd,i.pickFence);
    i.pickPending=true;
    return true;
}
std::optional<uint32_t> Renderer::pollPick() {
    auto& i=*impl_;
    if (!i.pickPending) return {};
    const auto status=vkGetFenceStatus(i.d.device,i.pickFence);
    if (status==VK_NOT_READY) return {};
    check(status,"Checking selection readback");
    check(vmaInvalidateAllocation(i.d.allocator,i.pickReadback.allocation,0,sizeof(uint32_t)),"Reading selection pixel");
    uint32_t token{};
    std::memcpy(&token,i.pickReadback.mapped,sizeof(token));
    i.pickPending=false;
    return token;
}
uint32_t Renderer::pick(int x, int y) {
    if (!impl_->drawn || x < 0 || y < 0 || static_cast<unsigned>(x) >= impl_->extent.width ||
        static_cast<unsigned>(y) >= impl_->extent.height)
        return 0;
    return impl_->read(x, y, 1, 1, false).tokens.front();
}
Capture Renderer::capture() {
    return impl_->read(0, 0, impl_->extent.width, impl_->extent.height, true);
}
const std::string& Renderer::adapter() const {
    return impl_->d.name;
}
double Renderer::lastCpuMilliseconds() const {
    return impl_->cpuMs;
}
double Renderer::lastGpuMilliseconds() const {
    return impl_->gpuMs;
}
unsigned Renderer::validationErrors() const {
    return impl_->d.errors.load();
}
unsigned Renderer::textureWarnings() const {
    return impl_->textures ? impl_->textures->warnings : 0;
}
std::string Renderer::diagnostics() const {
    std::ostringstream s;
    s << impl_->d.report() << " | " << impl_->extent.width << "x" << impl_->extent.height << " | "
      << (impl_->format == VK_FORMAT_B8G8R8A8_UNORM ? "BGRA8" : "RGBA8") << " SDR, D32, R32_UINT, "
      << (impl_->presentMode==VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO") << ", "
      << impl_->images.size() << " images, " << impl_->activeFrames << " frame slots"
      << " | objects culled " << impl_->lastCulled << " | attachment bytes " << impl_->attachmentBytes << ", peak " << impl_->peakAttachmentBytes
      << ", readback capacity " << impl_->readback.size << " | last CPU submit/present " << impl_->cpuMs
      << " ms, last completed GPU frame " << impl_->gpuMs << " ms, pipeline creation " << impl_->pipelineMs
      << " ms | base-color texture bindings " << (impl_->textures ? impl_->textures->entries.size() : 0)
      << ", GPU mip chains " << (impl_->textures ? impl_->textures->mipmapped : 0)
      << ", decode fallbacks " << textureWarnings();
    return s.str();
}
} // namespace platz::engine
