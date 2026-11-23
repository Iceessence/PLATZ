#pragma once
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_video.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace platz::engine::gpu {
void check(VkResult result, const char* operation);

enum class MemoryIntent { Mesh, Upload, Readback };
struct Buffer {
    VmaAllocator allocator{};
    VkBuffer handle{};
    VmaAllocation allocation{};
    void* mapped{};
    VkDeviceSize size{};
    Buffer() = default;
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};
struct Image {
    VkDevice device{};
    VmaAllocator allocator{};
    VkImage handle{};
    VmaAllocation allocation{};
    VkImageView view{};
    VkImageAspectFlags aspect{};
    Image() = default;
    ~Image();
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
};

struct Device {
    HWND window{};
    SDL_Window* host{};
    bool video{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    uint32_t timestampBits{};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VmaAllocator allocator{};
    bool physicalBudget{}, validation{}, rayTracing{}, meshShader{};
    std::atomic_uint errors{};
    std::string name, driver;
    PFN_vkSetDebugUtilsObjectNameEXT nameObject{};
    Device() = default;
    Device(const Device&) = delete;
    ~Device();
    void open(HWND hwnd);
    Buffer buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, MemoryIntent intent, const char* label);
    Image image(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                const char* label, uint32_t mipLevels=1);
    void label(VkObjectType type, uint64_t handle, const char* text) const;
    VkDeviceSize viewportBudget() const;
    std::string report() const;
};
} // namespace platz::engine::gpu
