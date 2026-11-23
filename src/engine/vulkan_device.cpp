#define VMA_IMPLEMENTATION
#include "vulkan_device.hpp"
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace platz::engine::gpu {
void check(VkResult r, const char* op) {
    if (r == VK_SUCCESS)
        return;
    std::string cause;
    switch (r) {
    case VK_ERROR_DEVICE_LOST:
        cause = "the graphics device was lost";
        break;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        cause = "graphics memory exhausted";
        break;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        cause = "host memory exhausted";
        break;
    case VK_ERROR_SURFACE_LOST_KHR:
        cause = "the viewport surface was lost";
        break;
    case VK_TIMEOUT:
        cause = "the GPU timed out";
        break;
    default:
        cause = "VkResult " + std::to_string(static_cast<int>(r));
        break;
    }
    throw std::runtime_error(std::string(op) + ": " + cause);
}
Buffer::~Buffer() {
    if (handle)
        vmaDestroyBuffer(allocator, handle, allocation);
}
Buffer::Buffer(Buffer&& b) noexcept {
    *this = std::move(b);
}
Buffer& Buffer::operator=(Buffer&& b) noexcept {
    std::swap(allocator, b.allocator);
    std::swap(handle, b.handle);
    std::swap(allocation, b.allocation);
    std::swap(mapped, b.mapped);
    std::swap(size, b.size);
    return *this;
}
Image::~Image() {
    if (view)
        vkDestroyImageView(device, view, nullptr);
    if (handle)
        vmaDestroyImage(allocator, handle, allocation);
}
Image::Image(Image&& b) noexcept {
    *this = std::move(b);
}
Image& Image::operator=(Image&& b) noexcept {
    std::swap(device, b.device);
    std::swap(allocator, b.allocator);
    std::swap(handle, b.handle);
    std::swap(allocation, b.allocation);
    std::swap(view, b.view);
    std::swap(aspect, b.aspect);
    return *this;
}

namespace {
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* message,
                                             void* context) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++static_cast<Device*>(context)->errors;
    std::fprintf(stderr, "PLATZ Vulkan %s [%s] %s\n",
                 severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "ERROR" : "WARNING",
                 message->pMessageIdName ? message->pMessageIdName : "layer",
                 message->pMessage ? message->pMessage : "");
    return VK_FALSE;
}
std::vector<VkExtensionProperties> extensions(VkPhysicalDevice physical = {}) {
    uint32_t count{};
    auto enumerate = [&](VkExtensionProperties* out) {
        return physical ? vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, out)
                        : vkEnumerateInstanceExtensionProperties(nullptr, &count, out);
    };
    check(enumerate(nullptr), "Enumerating Vulkan extensions");
    std::vector<VkExtensionProperties> all(count);
    check(enumerate(all.data()), "Reading Vulkan extensions");
    return all;
}
bool has(const std::vector<VkExtensionProperties>& list, const char* name) {
    return std::any_of(list.begin(), list.end(),
                       [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
}
std::string version(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}
bool supports(VkPhysicalDevice gpu, VkFormat format, VkFormatFeatureFlags flags) {
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
    return (props.optimalTilingFeatures & flags) == flags;
}
} // namespace

void Device::open(HWND hwnd) {
    window = hwnd;
    if (!IsWindow(hwnd))
        throw std::invalid_argument("Vulkan needs a valid native viewport.");
    uint32_t version{};
    check(vkEnumerateInstanceVersion(&version), "Querying Vulkan loader");
    if (version < VK_API_VERSION_1_4)
        throw std::runtime_error("A Vulkan 1.4 loader is required.");
    auto supported = extensions();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    video = true;
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props)
        throw std::runtime_error(SDL_GetError());
    bool configured = SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, hwnd) &&
                      SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true) &&
                      SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    if (configured)
        host = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!host)
        throw std::runtime_error(std::string("Opening SDL viewport: ") + SDL_GetError());
    Uint32 platformCount{};
    const char* const* platformExtensions = SDL_Vulkan_GetInstanceExtensions(&platformCount);
    if (!platformExtensions)
        throw std::runtime_error(SDL_GetError());
    std::vector<const char*> instanceExt(platformExtensions, platformExtensions + platformCount);
    bool debug = has(supported, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debug)
        instanceExt.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    char value[32]{};
    auto length = GetEnvironmentVariableA("PLATZ_VK_VALIDATION", value, sizeof(value));
    validation = length && length < sizeof(value) && std::strcmp(value, "0") != 0;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    if (validation) {
        uint32_t n{};
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> layers(n);
        check(vkEnumerateInstanceLayerProperties(&n, layers.data()), "Reading Vulkan layers");
        if (!debug || std::none_of(layers.begin(), layers.end(),
                                   [&](auto& l) { return std::strcmp(l.layerName, layer) == 0; }))
            throw std::runtime_error(
                "Requested Vulkan validation requires the Khronos validation layer and debug utilities.");
    }
    VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    dbg.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg.pfnUserCallback = debugCallback;
    dbg.pUserData = this;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "PLATZ";
    app.pEngineName = "PLATZ Engine";
    app.apiVersion = VK_API_VERSION_1_4;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(instanceExt.size());
    ci.ppEnabledExtensionNames = instanceExt.data();
    if (validation) {
        ci.ppEnabledLayerNames = &layer;
        ci.enabledLayerCount = 1;
        ci.pNext = &dbg;
    }
    check(vkCreateInstance(&ci, nullptr, &instance), "Creating Vulkan 1.4 instance");
    if (validation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        check(create(instance, &dbg, nullptr, &messenger), "Installing Vulkan diagnostic callback");
    }
    if (!SDL_Vulkan_CreateSurface(host, instance, nullptr, &surface))
        throw std::runtime_error(std::string("Creating SDL Vulkan surface: ") + SDL_GetError());
    uint32_t count{};
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Enumerating Vulkan devices");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Reading Vulkan devices");
    int best = -1;
    for (auto candidate : devices) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(candidate, &p);
        if (p.apiVersion < VK_API_VERSION_1_4 || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            continue;
        auto caps = extensions(candidate);
        if (!has(caps, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 available{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f13};
        vkGetPhysicalDeviceFeatures2(candidate, &available);
        if (!f13.dynamicRendering || !f13.synchronization2)
            continue;
        if (!supports(candidate, VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
            !supports(candidate, VK_FORMAT_R32_UINT,
                      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
            continue;
        VkSurfaceCapabilitiesKHR surfaceCaps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &surfaceCaps),
              "Querying presentation capabilities");
        if (!(surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            continue;
        uint32_t n{};
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
        for (uint32_t i = 0; i < n; ++i) {
            VkBool32 present{};
            check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present),
                  "Qualifying graphics queue");
            if (!present || !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                continue;
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score <= best)
                break;
            best = score;
            physical = candidate;
            family = i;
            properties = p;
            name = p.deviceName;
            timestampBits = families[i].timestampValidBits;
            break;
        }
    }
    if (!physical)
        throw std::runtime_error("No hardware Vulkan 1.4 device supports this viewport, dynamic rendering, "
                                 "synchronization2 and the required depth/ID formats.");
    VkPhysicalDeviceDriverProperties drv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 details{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &drv};
    vkGetPhysicalDeviceProperties2(physical, &details);
    driver = std::string(drv.driverName) + " " + drv.driverInfo;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    auto caps = extensions(physical);
    physicalBudget = has(caps, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 optional{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    if (has(caps, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
        rt.pNext = optional.pNext;
        optional.pNext = &rt;
    }
    if (has(caps, VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
        mesh.pNext = optional.pNext;
        optional.pNext = &mesh;
    }
    vkGetPhysicalDeviceFeatures2(physical, &optional);
    rayTracing = rt.rayTracingPipeline;
    meshShader = mesh.meshShader;
    std::vector<const char*> deviceExt{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (physicalBudget)
        deviceExt.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    float priority = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f.dynamicRendering = VK_TRUE;
    f.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f};
    dc.queueCreateInfoCount = 1;
    dc.pQueueCreateInfos = &q;
    dc.enabledExtensionCount = static_cast<uint32_t>(deviceExt.size());
    dc.ppEnabledExtensionNames = deviceExt.data();
    check(vkCreateDevice(physical, &dc, nullptr, &device), "Creating Vulkan logical device");
    vkGetDeviceQueue(device, family, 0, &queue);
    if (debug)
        nameObject = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
    VmaAllocatorCreateInfo ac{};
    ac.physicalDevice = physical;
    ac.device = device;
    ac.instance = instance;
    ac.vulkanApiVersion = VK_API_VERSION_1_4;
    if (physicalBudget)
        ac.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    check(vmaCreateAllocator(&ac, &allocator), "Creating Vulkan Memory Allocator");
}
Device::~Device() {
    if (allocator)
        vmaDestroyAllocator(allocator);
    if (device)
        vkDestroyDevice(device, nullptr);
    if (surface)
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    if (messenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroy(instance, messenger, nullptr);
    }
    if (instance)
        vkDestroyInstance(instance, nullptr);
    if (host)
        SDL_DestroyWindow(host);
    if (video)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
void Device::label(VkObjectType type, uint64_t handle, const char* text) const {
    if (!nameObject)
        return;
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = text;
    nameObject(device, &info);
}
Buffer Device::buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, MemoryIntent intent, const char* name) {
    Buffer b;
    b.allocator = allocator;
    b.size = bytes;
    VkBufferCreateInfo desc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    desc.size = bytes;
    desc.usage = usage;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
    if (intent == MemoryIntent::Mesh)
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    else {
        ai.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ai.flags |= intent == MemoryIntent::Readback ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                                     : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    VmaAllocationInfo info{};
    check(vmaCreateBuffer(allocator, &desc, &ai, &b.handle, &b.allocation, &info), name);
    b.mapped = info.pMappedData;
    label(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(b.handle), name);
    return b;
}
Image Device::image(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                    const char* name, uint32_t mipLevels) {
    Image img;
    img.device = device;
    img.allocator = allocator;
    img.aspect = aspect;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = {extent.width, extent.height, 1};
    ci.format = format;
    ci.mipLevels = mipLevels;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    ai.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
    check(vmaCreateImage(allocator, &ci, &ai, &img.handle, &img.allocation, nullptr), name);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img.handle;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, mipLevels, 0, 1};
    check(vkCreateImageView(device, &vi, nullptr, &img.view), "Creating image view");
    label(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(img.handle), name);
    return img;
}
VkDeviceSize Device::viewportBudget() const {
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(allocator, budgets);
    VkDeviceSize largest = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if (!(memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
            continue;
        const auto& b = budgets[i];
        largest = std::max(largest, b.budget > b.usage ? b.budget - b.usage : VkDeviceSize{0});
    }
    return std::min<VkDeviceSize>(512ull << 20, largest / 3);
}
std::string Device::report() const {
    std::ostringstream s;
    s << "Vulkan " << version(properties.apiVersion) << " | " << name << " | " << driver
      << " | graphics+present queue " << family << " | maxImageDimension2D "
      << properties.limits.maxImageDimension2D << " | dynamic rendering, synchronization2 | ray pipeline "
      << (rayTracing ? "available" : "unavailable") << ", mesh shaders "
      << (meshShader ? "available" : "unavailable") << " (both disabled)"
      << " | validation " << (validation ? "on" : "off") << " (" << errors.load() << " errors)";
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(allocator, budgets);
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        const auto& b = budgets[i];
        s << " | heap " << i
          << ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " device-local" : " host")
          << " capacity " << memory.memoryHeaps[i].size << ", " << (physicalBudget ? "driver" : "estimated")
          << " budget " << b.budget << ", heap usage estimate " << b.usage << ", VMA reserved "
          << b.statistics.blockBytes << ", allocations " << b.statistics.allocationBytes << " bytes ("
          << b.statistics.allocationCount << " resources)";
    }
    return s.str();
}
} // namespace platz::engine::gpu
