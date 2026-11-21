#include "renderer.hpp"
#include "vulkan_device.hpp"
#include "scene_spv.hpp"
#include <DirectXMath.h>
#include <algorithm>
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
constexpr float fov = XMConvertToRadians(47.f);
XMVECTOR vector(Vec3 v) {
    return XMVectorSet(v[0], v[1], v[2], 0);
}
XMVECTOR offset(const Camera& camera) {
    float cp = std::cos(camera.pitch);
    return XMVectorSet(std::sin(camera.yaw) * cp, std::sin(camera.pitch), std::cos(camera.yaw) * cp, 0);
}

struct Vertex {
    XMFLOAT3 position, normal;
};

struct MeshRange {
    UINT startIndex, count;
};
struct Geometry {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<MeshRange, 4> ranges{};
    UINT vertex(XMFLOAT3 p, XMFLOAT3 n) {
        vertices.push_back({p, n});
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
                  VkPipelineStageFlags2 destination, VkAccessFlags2 writes) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = source;
    b.srcAccessMask = reads;
    b.dstStageMask = destination;
    b.dstAccessMask = writes;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1;
    d.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &d);
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
    Image color, ids, depth;
    bool rendered{};
};
struct PresentImage {
    VkImage image{};
    VkSemaphore finished{};
};
} // namespace
struct Renderer::Impl {
    Device d; // Outlives every buffer, image and in-flight frame.
    std::array<Frame, framesInFlight> frames;
    uint32_t current{}, last{}, frameNumber{};
    VkDescriptorSetLayout binding{};
    VkDescriptorPool descriptors{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{}, ground{};
    VkCommandPool transferPool{};
    VkCommandBuffer transferCmd{};
    VkFence transferFence{};
    Buffer geometry, readback;
    Buffer pendingGeometry, geometryStaging;
    VkDeviceSize indexOffset{}, stride{};
    std::array<MeshRange, 4> ranges{};
    std::map<std::string,MeshRange> imported;
    VkSwapchainKHR chain{};
    std::vector<PresentImage> images;
    VkExtent2D extent{}, requested{};
    VkFormat format{};
    bool pendingResize{}, drawn{};
    double cpuMs{}, gpuMs{}, pipelineMs{};
    VkDeviceSize attachmentBytes{}, peakAttachmentBytes{}, maxViewportBytes{};
    void open(HWND);
    void rebuild();
    void releaseSwap();
    void makePipelines();
    void upload(const std::map<std::string,const platz::MeshAsset*>& assets);
    void finishTransfer();
    void prepare(Frame&, std::span<const DrawObject>, const Camera&, uint32_t);
    void record(Frame&, std::span<const DrawObject>, uint32_t);
    void draw(std::span<const DrawObject>, const Camera&, uint32_t);
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
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &binding;
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
    if (pipeline)
        vkDestroyPipeline(d.device, pipeline, nullptr);
    if (ground)
        vkDestroyPipeline(d.device, ground, nullptr);
    if (layout)
        vkDestroyPipelineLayout(d.device, layout, nullptr);
    if (descriptors)
        vkDestroyDescriptorPool(d.device, descriptors, nullptr);
    if (binding)
        vkDestroyDescriptorSetLayout(d.device, binding, nullptr);
}
void Renderer::Impl::finishTransfer() {
    check(vkEndCommandBuffer(transferCmd), "Closing transfer commands");
    VkCommandBufferSubmitInfo cmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd.commandBuffer = transferCmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd;
    check(vkResetFences(d.device, 1, &transferFence), "Resetting transfer completion");
    check(vkQueueSubmit2(d.queue, 1, &submit, transferFence), "Submitting GPU transfer");
    check(vkWaitForFences(d.device, 1, &transferFence, VK_TRUE, timeout), "Waiting for GPU transfer");
}
void Renderer::Impl::upload(const std::map<std::string,const platz::MeshAsset*>& assets) {
    auto source = makeGeometry();
    std::map<std::string,MeshRange> newRanges;
    for(const auto& [digest,asset]:assets) {
        const auto base=static_cast<uint32_t>(source.vertices.size());
        newRanges.emplace(digest,MeshRange{static_cast<UINT>(source.indices.size()),static_cast<UINT>(asset->indices().size())});
        for(const auto& v:asset->vertices()) source.vertex(
            {v.position[0],v.position[1],v.position[2]}, {v.normal[0],v.normal[1],v.normal[2]});
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
    uint32_t imageCount = std::max(caps.minImageCount + 1, framesInFlight);
    if (caps.maxImageCount)
        imageCount = std::min(imageCount, caps.maxImageCount);
    const uint64_t pixels = uint64_t(extent.width) * extent.height;
    if (pixels * (framesInFlight * 12ull + imageCount * 4ull) > std::min(d.viewportBudget(), maxViewportBytes))
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
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(d.device, &ci, nullptr, &chain), "Creating FIFO viewport swapchain");
    check(vkGetSwapchainImagesKHR(d.device, chain, &count, nullptr), "Counting presentation images");
    std::vector<VkImage> native(count);
    check(vkGetSwapchainImagesKHR(d.device, chain, &count, native.data()), "Reading presentation images");
    images.resize(count);
    VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < count; ++i) {
        images[i].image = native[i];
        check(vkCreateSemaphore(d.device, &sc, nullptr, &images[i].finished),
              "Creating per-image presentation semaphore");
    }
    for (auto& f : frames) {
        const auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        f.color = d.image(extent, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, "Viewport display color");
        f.ids = d.image(extent, idFormat, usage, VK_IMAGE_ASPECT_COLOR_BIT, "Scene identity attachment");
        f.depth = d.image(extent, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT, "Scene depth");
    }
    attachmentBytes = pixels * framesInFlight * 12ull;
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
                                                  {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &stream;
    vertex.vertexAttributeDescriptionCount = 2;
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
    d.label(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline), "Opaque meshes and identity");
    pipelineMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

void Renderer::Impl::prepare(Frame& f, std::span<const DrawObject> objects, const Camera& camera,
                             uint32_t selected) {
    static_assert(offsetof(Constants, viewProjection) == shaders::offset::viewProjection &&
                  offsetof(Constants, world) == shaders::offset::world);
    static_assert(offsetof(Constants, normal) == shaders::offset::normalMatrix &&
                  offsetof(Constants, color) == shaders::offset::baseColor);
    static_assert(offsetof(Constants, eye) == shaders::offset::eye &&
                  offsetof(Constants, selected) == shaders::offset::selected);
    static_assert(offsetof(Constants, token) == shaders::offset::token &&
                  offsetof(Constants, mesh) == shaders::offset::meshKind);
    static_assert(sizeof(Constants) <= shaders::sceneSize && shaders::sceneSet == 0);
    auto slots = objects.size() + 1;
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
    auto center = vector(camera.target), eye = center + offset(camera) * camera.distance;
    auto view = XMMatrixLookAtRH(eye, center, XMVectorSet(0, 1, 0, 0));
    auto projection = XMMatrixPerspectiveFovRH(fov, float(extent.width) / float(extent.height),
        std::max(.00001f,camera.distance*.001f),std::max(2000.f,camera.distance*5.f));
    XMStoreFloat4x4(&c.viewProjection, view * projection);
    XMStoreFloat3(&c.eye, eye);
    c.selected = selected;
    auto store = [&](size_t i, XMMATRIX world) {
        XMStoreFloat4x4(&c.world, world);
        XMStoreFloat4x4(&c.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
        std::memcpy(static_cast<std::byte*>(f.uniforms.mapped) + i * stride, &c, sizeof(c));
    };
    c.mesh = 3;
    store(0, XMMatrixScaling(1800, 1, 1800) * XMMatrixTranslation(camera.target[0], -.012f, camera.target[2]));
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto& o = objects[i];
        auto rotation = XMMatrixRotationX(XMConvertToRadians(o.rotation[0])) *
                        XMMatrixRotationY(XMConvertToRadians(o.rotation[1])) *
                        XMMatrixRotationZ(XMConvertToRadians(o.rotation[2]));
        c.mesh = static_cast<uint32_t>(o.mesh);
        c.color = o.color;
        c.token = o.token;
        store(i + 1, XMMatrixScalingFromVector(vector(o.scale)) * rotation *
                         XMMatrixTranslationFromVector(vector(o.position)));
    }
    check(vmaFlushAllocation(d.allocator, f.uniforms.allocation, 0, used), "Flushing scene parameters");
}

void Renderer::Impl::record(Frame& f, std::span<const DrawObject> objects, uint32_t imageIndex) {
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
    dep.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
    auto submit = [&](size_t i, MeshRange range) {
        uint32_t off = static_cast<uint32_t>(stride * i);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, shaders::sceneSet, 1,
                                &f.constants, 1, &off);
        vkCmdDrawIndexed(cmd, range.count, 1, range.startIndex, 0, 0);
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ground);
    submit(0, ranges[3]);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].token && objects[i].asset)
            submit(i+1,imported.at(objects[i].asset->digest()));
        else if (objects[i].token && static_cast<uint32_t>(objects[i].mesh) < 3)
            submit(i + 1, ranges[static_cast<uint32_t>(objects[i].mesh)]);
    vkCmdEndRendering(cmd);
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
void Renderer::Impl::draw(std::span<const DrawObject> objects, const Camera& camera, uint32_t selected) {
    auto begin = Clock::now();
    if (pendingResize)
        rebuild();
    if (!chain)
        return;
    std::map<std::string,const platz::MeshAsset*> assets;
    for(const auto& object:objects) if(object.asset) assets.try_emplace(object.asset->digest(),object.asset.get());
    if(assets.size()!=imported.size() || !std::equal(assets.begin(),assets.end(),imported.begin(),
            [](const auto& a,const auto& b){return a.first==b.first;})) upload(assets);
    auto& f = frames[current];
    check(vkWaitForFences(d.device, 1, &f.fence, VK_TRUE, timeout), "Reusing completed frame");
    if (f.rendered && f.timing) {
        uint64_t times[2]{};
        check(vkGetQueryPoolResults(d.device, f.timing, 0, 2, sizeof(times), times, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT),
              "Collecting GPU timing");
        const uint64_t mask = d.timestampBits == 64 ? UINT64_MAX : ((uint64_t{1} << d.timestampBits) - 1);
        gpuMs = double((times[1] - times[0]) & mask) * d.properties.limits.timestampPeriod / 1e6;
    }
    vmaSetCurrentFrameIndex(d.allocator, ++frameNumber);
    prepare(f, objects, camera, selected);
    uint32_t idx{};
    VkResult acquired = vkAcquireNextImageKHR(d.device, chain, timeout, f.acquired, {}, &idx);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        pendingResize = true;
        drawn = false;
        return;
    }
    if (acquired != VK_SUBOPTIMAL_KHR)
        check(acquired, "Acquiring display image");
    pendingResize = acquired == VK_SUBOPTIMAL_KHR;
    record(f, objects, idx);
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
    last = current;
    current = (current + 1) % framesInFlight;
    drawn = result != VK_ERROR_OUT_OF_DATE_KHR;
    cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    if (d.errors.load())
        throw std::runtime_error("Vulkan validation reported an error. See the validation log.");
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
Renderer::Renderer(void* window, uint64_t maxViewportBytes) : impl_(std::make_unique<Impl>()) {
    impl_->maxViewportBytes = maxViewportBytes;
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
void Renderer::draw(std::span<const DrawObject> objects, const Camera& c, uint32_t selected) {
    impl_->draw(objects, c, selected);
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
std::string Renderer::diagnostics() const {
    std::ostringstream s;
    s << impl_->d.report() << " | " << impl_->extent.width << "x" << impl_->extent.height << " | "
      << (impl_->format == VK_FORMAT_B8G8R8A8_UNORM ? "BGRA8" : "RGBA8") << " SDR, D32, R32_UINT, FIFO, "
      << impl_->images.size() << " images"
      << " | attachment bytes " << impl_->attachmentBytes << ", peak " << impl_->peakAttachmentBytes
      << ", readback capacity " << impl_->readback.size << " | last CPU submit/present " << impl_->cpuMs
      << " ms, last completed GPU frame " << impl_->gpuMs << " ms, pipeline creation " << impl_->pipelineMs
      << " ms";
    return s.str();
}
} // namespace platz::engine
