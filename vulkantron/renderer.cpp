#include "renderer.hpp"
#include "faithful_frame.hpp"

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <png.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace vt {
namespace {
constexpr std::size_t max_vertices = 4 * 1024 * 1024;
constexpr VkDeviceSize max_readback_bytes = 256ull * 1024 * 1024;

template<class T> T info(VkStructureType type) {
    T result{};
    result.sType = type;
    return result;
}
void require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
}
template<class T, class F> std::vector<T> enumerate(F function, const char* operation) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::uint32_t count = 0;
        require(function(&count, static_cast<T*>(nullptr)), operation);
        std::vector<T> values(count);
        const VkResult result = function(&count, values.data());
        if (result == VK_INCOMPLETE) continue;
        require(result, operation);
        values.resize(count);
        return values;
    }
    throw std::runtime_error(std::string(operation) + " changed repeatedly during enumeration");
}
bool extension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& item) {
        return std::strcmp(item.extensionName, name) == 0;
    });
}
bool srgb(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}
float srgb_channel(float linear) {
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}
std::vector<std::uint32_t> shader_words(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open shader: " + path.string());
    const auto bytes = file.tellg();
    if (bytes < 4 || bytes > 8 * 1024 * 1024 || bytes % 4 != 0)
        throw std::runtime_error("Invalid SPIR-V size: " + path.string());
    std::vector<std::uint32_t> result(static_cast<std::size_t>(bytes) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(result.data()), bytes) || result[0] != 0x07230203u)
        throw std::runtime_error("Invalid SPIR-V module: " + path.string());
    return result;
}
void write_png(const std::filesystem::path& path, std::uint32_t width,
               std::uint32_t height, const unsigned char* pixels, bool bgra) {
    // Presentation bytes are already sRGB encoded for both chosen formats.
    std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * height * 3);
    for (std::size_t p = 0; p < static_cast<std::size_t>(width) * height; ++p) {
        rgb[p * 3] = pixels[p * 4 + (bgra ? 2 : 0)];
        rgb[p * 3 + 1] = pixels[p * 4 + 1];
        rgb[p * 3 + 2] = pixels[p * 4 + (bgra ? 0 : 2)];
    }
    const std::string filename = path.string();
    FILE* output = std::fopen(filename.c_str(), "wbx");
    if (!output) throw std::runtime_error("Cannot exclusively create screenshot: " + filename);
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop metadata = png ? png_create_info_struct(png) : nullptr;
    if (!png || !metadata) {
        if (png) png_destroy_write_struct(&png, nullptr);
        std::fclose(output);
        std::remove(filename.c_str());
        throw std::runtime_error("Cannot allocate PNG writer");
    }
    // No nontrivial C++ automatic objects are created across libpng's longjmp.
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &metadata);
        std::fclose(output);
        std::remove(filename.c_str());
        throw std::runtime_error("PNG write failed: " + filename);
    }
    png_init_io(png, output);
    png_set_IHDR(png, metadata, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_sRGB(png, metadata, PNG_sRGB_INTENT_PERCEPTUAL);
    png_write_info(png, metadata);
    // Positive Vulkan viewport height puts row zero at the top of the image.
    for (std::uint32_t y = 0; y < height; ++y)
        png_write_row(png, rgb.data() + static_cast<std::size_t>(y) * width * 3);
    png_write_end(png, metadata);
    png_destroy_write_struct(&png, &metadata);
    if (std::fclose(output) != 0) {
        std::remove(filename.c_str());
        throw std::runtime_error("Cannot finish screenshot: " + filename);
    }
}
} // namespace

struct Renderer::Impl {
    struct Buffer {
        VkBuffer handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;
        bool coherent = false;
    } vertices, readback;
    struct PresentImage {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSemaphore finished = VK_NULL_HANDLE;
        VkFence present_fence = VK_NULL_HANDLE;
        bool present_pending = false;
    };
    SDL_Window* window = nullptr;
    std::filesystem::path shaders;
    mutable RenderStats statistics;
    std::atomic<std::uint32_t> validation_errors{0};
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkPhysicalDeviceProperties physical_properties{};
    VkPhysicalDeviceFeatures enabled_features{};
    bool line_rasterization = false, smooth_lines = false, bresenham_lines = false;
    bool native_clip_depth = false, force_standard_depth = false;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics = VK_NULL_HANDLE, present = VK_NULL_HANDLE;
    std::uint32_t graphics_family = 0, present_family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    VkFence acquire_fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    bool submission_pending = false;
    bool acquire_unconsumed = false;
    bool shutdown_complete = false;
    bool maintenance_instance = false, maintenance = false;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<PresentImage> images;
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED, depth_format = VK_FORMAT_UNDEFINED;
    VkImage depth_image = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    bool depth_used = false, dirty = true;
    int drawable_width = 0, drawable_height = 0;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    struct FaithfulTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        std::shared_ptr<const Texture> source;
        VkDeviceSize bytes = 0;
    };
    using TextureKey = std::pair<std::uint32_t,std::uint64_t>;
    std::map<TextureKey,FaithfulTexture> faithful_textures;
    std::map<std::array<std::uint32_t,16>,VkPipeline> faithful_pipelines;
    VkDescriptorSetLayout faithful_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool faithful_descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout faithful_layout = VK_NULL_HANDLE;
    VkShaderModule faithful_vertex_shader = VK_NULL_HANDLE, faithful_fragment_shader = VK_NULL_HANDLE;
    VkImage faithful_color = VK_NULL_HANDLE, faithful_depth = VK_NULL_HANDLE;
    VkDeviceMemory faithful_color_memory = VK_NULL_HANDLE, faithful_depth_memory = VK_NULL_HANDLE;
    VkImageView faithful_color_view = VK_NULL_HANDLE, faithful_depth_view = VK_NULL_HANDLE;
    VkFormat faithful_color_format = VK_FORMAT_UNDEFINED, faithful_depth_format = VK_FORMAT_UNDEFINED;
    bool faithful_mode = false, faithful_used = false;
    std::vector<Buffer> faithful_uploads;
    FaithfulTexture bloom_source;
    VkPipeline bloom_pipeline = VK_NULL_HANDLE;
    VkShaderModule bloom_vertex_shader = VK_NULL_HANDLE, bloom_fragment_shader = VK_NULL_HANDLE;
    bool bloom_used = false;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
            void* user) {
        auto* self = static_cast<Impl*>(user);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            self->validation_errors.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[Vulkan validation %s] %s\n",
                     severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "ERROR" : "warning",
                     data && data->pMessage ? data->pMessage : "(empty diagnostic)");
        return VK_FALSE;
    }
    void init(SDL_Window* selected_window, const std::filesystem::path& selected_shaders,
              bool validation) {
        window = selected_window;
        shaders = selected_shaders;
        if (!window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN))
            throw std::runtime_error("Renderer requires an SDL_WINDOW_VULKAN window");
        std::uint32_t loader_version = 0;
        require(vkEnumerateInstanceVersion(&loader_version), "Query Vulkan loader version");
        if (loader_version < VK_API_VERSION_1_3)
            throw std::runtime_error("Vulkan 1.3 or newer is required");
        const auto available = enumerate<VkExtensionProperties>([](auto* count, auto* values) {
            return vkEnumerateInstanceExtensionProperties(nullptr, count, values);
        }, "Enumerate instance extensions");
        Uint32 sdl_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
        if (!sdl_extensions) throw std::runtime_error(SDL_GetError());
        std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_count);
        std::vector<const char*> layers;
        auto debug_info = info<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        if (validation) {
            const auto available_layers = enumerate<VkLayerProperties>([](auto* count, auto* values) {
                return vkEnumerateInstanceLayerProperties(count, values);
            }, "Enumerate instance layers");
            if (std::none_of(available_layers.begin(), available_layers.end(), [](const auto& item) {
                    return std::strcmp(item.layerName, "VK_LAYER_KHRONOS_validation") == 0;
                }) || !extension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
                throw std::runtime_error("Requested Vulkan validation layer/debug utils are unavailable");
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_info.pfnUserCallback = debug;
            debug_info.pUserData = this;
        }
        maintenance_instance = extension(available, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) &&
                               extension(available, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        if (maintenance_instance) {
            extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
            extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        }
        auto app = info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "VulkanTron";
        app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app.pEngineName = "VulkanTron direct Vulkan reference";
        app.apiVersion = VK_API_VERSION_1_3;
        auto create = info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        create.pApplicationInfo = &app;
        create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        create.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        create.ppEnabledLayerNames = layers.data();
        create.pNext = validation ? &debug_info : nullptr;
        require(vkCreateInstance(&create, nullptr, &instance), "Create Vulkan instance");
        if (validation) {
            const auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (!create_debug) throw std::runtime_error("Cannot load validation messenger entry point");
            require(create_debug(instance, &debug_info, nullptr, &messenger), "Create validation messenger");
        }
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
            throw std::runtime_error(std::string("Create Vulkan surface: ") + SDL_GetError());
        choose_device();
        auto pool_info = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = graphics_family;
        require(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "Create command pool");
        auto allocation = info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        require(vkAllocateCommandBuffers(device, &allocation, &command), "Allocate command buffer");
        auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        require(vkCreateFence(device, &fence, nullptr, &submitted), "Create submit fence");
        fence.flags = 0;
        require(vkCreateFence(device, &fence, nullptr, &acquire_fence), "Create acquire fence");
        auto semaphore = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        require(vkCreateSemaphore(device, &semaphore, nullptr, &acquired), "Create acquisition semaphore");
        std::fprintf(stderr, "[Vulkan] %s; API 1.3 dynamic rendering; present fences %s\n",
                     statistics.device.c_str(), maintenance ? "enabled" : "unavailable (WaitIdle retirement fallback)");
    }

    void choose_device() {
        const auto devices = enumerate<VkPhysicalDevice>([this](auto* count, auto* values) {
            return vkEnumeratePhysicalDevices(instance, count, values);
        }, "Enumerate Vulkan devices");
        int best_score = -1;
        for (const auto candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;
            const auto extensions = enumerate<VkExtensionProperties>([candidate](auto* count, auto* values) {
                return vkEnumerateDeviceExtensionProperties(candidate, nullptr, count, values);
            }, "Enumerate device extensions");
            if (!extension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
            auto features13 = info<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
            auto features = info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;
            std::uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, queues.data());
            int graphics_index = -1, present_index = -1;
            for (std::uint32_t i = 0; i < count; ++i) {
                VkBool32 supports_present = VK_FALSE;
                require(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supports_present),
                        "Query presentation queue support");
                if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    graphics_index = static_cast<int>(i);
                    if (supports_present) { present_index = static_cast<int>(i); break; }
                }
                if (queues[i].queueCount && supports_present) present_index = static_cast<int>(i);
            }
            if (graphics_index < 0 || present_index < 0) continue;
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
            if (score <= best_score) continue;
            best_score = score;
            physical = candidate;
            graphics_family = static_cast<std::uint32_t>(graphics_index);
            present_family = static_cast<std::uint32_t>(present_index);
            statistics.device = properties.deviceName;
        }
        if (!physical) throw std::runtime_error("No Vulkan 1.3 graphics/presentation device with dynamic rendering");
        const auto supported = enumerate<VkExtensionProperties>([this](auto* count, auto* values) {
            return vkEnumerateDeviceExtensionProperties(physical, nullptr, count, values);
        }, "Enumerate selected device extensions");
        vkGetPhysicalDeviceProperties(physical, &physical_properties);
        statistics.max_texture_size = physical_properties.limits.maxImageDimension2D;
        VkPhysicalDeviceFeatures supported_features{};
        vkGetPhysicalDeviceFeatures(physical, &supported_features);
        enabled_features.fillModeNonSolid = supported_features.fillModeNonSolid;
        enabled_features.largePoints = supported_features.largePoints;
        enabled_features.wideLines = supported_features.wideLines;
        enabled_features.samplerAnisotropy = supported_features.samplerAnisotropy;
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        auto line_features = info<VkPhysicalDeviceLineRasterizationFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT);
        if (extension(supported, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME)) {
            auto query = info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
            query.pNext = &line_features;
            vkGetPhysicalDeviceFeatures2(physical, &query);
            smooth_lines = line_features.smoothLines;
            bresenham_lines = line_features.bresenhamLines;
            line_features.rectangularLines = VK_FALSE;
            line_features.stippledRectangularLines = VK_FALSE;
            line_features.stippledBresenhamLines = VK_FALSE;
            line_features.stippledSmoothLines = VK_FALSE;
            line_rasterization = smooth_lines || bresenham_lines;
            if (line_rasterization) extensions.push_back(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME);
        }
        auto clip_features = info<VkPhysicalDeviceDepthClipControlFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT);
        const char* clip_setting = SDL_getenv("VULKANTRON_NATIVE_CLIP_DEPTH");
        force_standard_depth = clip_setting && std::strcmp(clip_setting, "0") == 0;
        if (!force_standard_depth && extension(supported, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME)) {
            auto query = info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
            query.pNext = &clip_features;
            vkGetPhysicalDeviceFeatures2(physical, &query);
            native_clip_depth = clip_features.depthClipControl;
            if (native_clip_depth) extensions.push_back(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
        }
        auto maintenance_features = info<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT);
        if (maintenance_instance && extension(supported, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
            auto query = info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
            query.pNext = &maintenance_features;
            vkGetPhysicalDeviceFeatures2(physical, &query);
            maintenance = maintenance_features.swapchainMaintenance1 == VK_TRUE;
            if (maintenance) extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        }
        if (extension(supported, "VK_KHR_portability_subset")) extensions.push_back("VK_KHR_portability_subset");
        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queue_info;
        for (auto family : {graphics_family, present_family}) {
            if (!queue_info.empty() && family == graphics_family) continue;
            auto queue = info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            queue_info.push_back(queue);
        }
        auto features13 = info<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        if (native_clip_depth) {
            clip_features.pNext = features13.pNext;
            features13.pNext = &clip_features;
        }
        if (line_rasterization) {
            line_features.pNext = features13.pNext;
            features13.pNext = &line_features;
        }
        if (maintenance) {
            maintenance_features.pNext = features13.pNext;
            features13.pNext = &maintenance_features;
        }
        auto create = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        create.pNext = &features13;
        create.pEnabledFeatures = &enabled_features;
        create.queueCreateInfoCount = static_cast<std::uint32_t>(queue_info.size());
        create.pQueueCreateInfos = queue_info.data();
        create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        require(vkCreateDevice(physical, &create, nullptr, &device), "Create Vulkan device");
        vkGetDeviceQueue(device, graphics_family, 0, &graphics);
        vkGetDeviceQueue(device, present_family, 0, &present);
        vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
        for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depth_format = format;
                break;
            }
        }
        if (depth_format == VK_FORMAT_UNDEFINED) throw std::runtime_error("No supported depth attachment format");
        for (auto format : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                faithful_depth_format = format;
                statistics.depth_bits = format == VK_FORMAT_D24_UNORM_S8_UINT ? 24 : 32;
                statistics.stencil_bits = 8;
                break;
            }
        }
    }

    std::uint32_t memory_type(std::uint32_t allowed, VkMemoryPropertyFlags required,
                              VkMemoryPropertyFlags preferred = 0) const {
        std::uint32_t fallback = UINT32_MAX;
        for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
            const auto flags = memory_properties.memoryTypes[i].propertyFlags;
            if ((allowed & (1u << i)) && (flags & required) == required) {
                if ((flags & preferred) == preferred) return i;
                fallback = i;
            }
        }
        if (fallback == UINT32_MAX) throw std::runtime_error("No suitable Vulkan memory type");
        return fallback;
    }
    void destroy_buffer(Buffer& buffer) noexcept {
        if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
        if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
        buffer = {};
    }
    void ensure_buffer(Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage) {
        if (bytes <= buffer.capacity) return;
        Buffer replacement;
        try {
            auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
            create.size = bytes;
            create.usage = usage;
            create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            require(vkCreateBuffer(device, &create, nullptr, &replacement.handle), "Create host buffer");
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, replacement.handle, &requirements);
            const auto type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | (usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0));
            auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = type;
            require(vkAllocateMemory(device, &allocation, nullptr, &replacement.memory), "Allocate host buffer memory");
            require(vkBindBufferMemory(device, replacement.handle, replacement.memory, 0), "Bind host buffer memory");
            require(vkMapMemory(device, replacement.memory, 0, VK_WHOLE_SIZE, 0, &replacement.mapped), "Map host buffer");
            replacement.capacity = bytes;
            replacement.coherent = (memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        } catch (...) { destroy_buffer(replacement); throw; }
        destroy_buffer(buffer);
        buffer = replacement;
    }
    void host_memory(const Buffer& buffer, bool invalidate) {
        if (buffer.coherent) return;
        auto range = info<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
        range.memory = buffer.memory;
        range.size = VK_WHOLE_SIZE;
        require(invalidate ? vkInvalidateMappedMemoryRanges(device, 1, &range) :
                             vkFlushMappedMemoryRanges(device, 1, &range),
                invalidate ? "Invalidate readback memory" : "Flush vertex memory");
    }
    VkImageView image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
        auto create = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        create.image = image;
        create.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create.format = format;
        create.subresourceRange = {aspect, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        require(vkCreateImageView(device, &create, nullptr, &view), "Create image view");
        return view;
    }
    void create_depth() {
        auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        create.imageType = VK_IMAGE_TYPE_2D;
        create.format = depth_format;
        create.extent = {extent.width, extent.height, 1};
        create.mipLevels = create.arrayLayers = 1;
        create.samples = VK_SAMPLE_COUNT_1_BIT;
        create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        require(vkCreateImage(device, &create, nullptr, &depth_image), "Create depth image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, depth_image, &requirements);
        auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        require(vkAllocateMemory(device, &allocation, nullptr, &depth_memory), "Allocate depth memory");
        require(vkBindImageMemory(device, depth_image, depth_memory, 0), "Bind depth memory");
        depth_view = image_view(depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
        depth_used = false;
    }

    void create_pipeline() {
        VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
        try {
            const auto vertex_words = shader_words(shaders / "scene.vert.spv");
            const auto fragment_words = shader_words(shaders / "scene.frag.spv");
            auto module = info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
            module.codeSize = vertex_words.size() * 4;
            module.pCode = vertex_words.data();
            require(vkCreateShaderModule(device, &module, nullptr, &vertex), "Create vertex shader");
            module.codeSize = fragment_words.size() * 4;
            module.pCode = fragment_words.data();
            require(vkCreateShaderModule(device, &module, nullptr, &fragment), "Create fragment shader");
            VkPipelineShaderStageCreateInfo stages[2]{};
            for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment;
            VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 68};
            auto layout = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
            layout.pushConstantRangeCount = 1;
            layout.pPushConstantRanges = &push;
            require(vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout), "Create pipeline layout");
            const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const VkVertexInputAttributeDescription attributes[] = {
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, color))}
            };
            auto input = info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
            input.vertexBindingDescriptionCount = 1; input.pVertexBindingDescriptions = &binding;
            input.vertexAttributeDescriptionCount = 2; input.pVertexAttributeDescriptions = attributes;
            auto assembly = info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            auto viewport = info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
            viewport.viewportCount = viewport.scissorCount = 1;
            auto raster = info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0f;
            auto multisample = info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            auto depth = info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
            depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            auto blend = info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
            blend.attachmentCount = 1; blend.pAttachments = &attachment;
            const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            auto dynamic = info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
            dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
            auto rendering = info<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
            rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &color_format;
            rendering.depthAttachmentFormat = depth_format;
            auto create = info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
            create.pNext = &rendering;
            create.stageCount = 2; create.pStages = stages;
            create.pVertexInputState = &input; create.pInputAssemblyState = &assembly;
            create.pViewportState = &viewport; create.pRasterizationState = &raster;
            create.pMultisampleState = &multisample; create.pDepthStencilState = &depth;
            create.pColorBlendState = &blend; create.pDynamicState = &dynamic;
            create.layout = pipeline_layout;
            require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline), "Create scene pipeline");
        } catch (...) {
            if (vertex) vkDestroyShaderModule(device, vertex, nullptr);
            if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);
    }

    void idle() {
        if (!device) return;
        require(vkDeviceWaitIdle(device), "Wait for Vulkan device");
        submission_pending = false;
        if (maintenance) for (auto& image : images) {
            if (!image.present_pending) continue;
            require(vkWaitForFences(device, 1, &image.present_fence, VK_TRUE, UINT64_MAX), "Wait for presentation fence");
            image.present_pending = false;
        }
    }
    void destroy_swapchain() noexcept {
        destroy_faithful_target();
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        pipeline = VK_NULL_HANDLE; pipeline_layout = VK_NULL_HANDLE;
        if (depth_view) vkDestroyImageView(device, depth_view, nullptr);
        if (depth_image) vkDestroyImage(device, depth_image, nullptr);
        if (depth_memory) vkFreeMemory(device, depth_memory, nullptr);
        depth_view = VK_NULL_HANDLE; depth_image = VK_NULL_HANDLE; depth_memory = VK_NULL_HANDLE;
        for (auto& image : images) {
            if (image.view) vkDestroyImageView(device, image.view, nullptr);
            if (image.finished) vkDestroySemaphore(device, image.finished, nullptr);
            if (image.present_fence) vkDestroyFence(device, image.present_fence, nullptr);
        }
        images.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    bool recreate(int width, int height) {
        if (width <= 0 || height <= 0) return false;
        VkSurfaceCapabilitiesKHR capabilities{};
        require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities), "Query surface capabilities");
        VkExtent2D next = capabilities.currentExtent;
        if (next.width == UINT32_MAX) {
            next.width = std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            next.height = std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        if (!next.width || !next.height) return false;
        if (static_cast<VkDeviceSize>(next.width) * next.height > max_readback_bytes / 4)
            throw std::runtime_error("Drawable exceeds the bounded 256 MiB capture allocation");
        const auto formats = enumerate<VkSurfaceFormatKHR>([this](auto* count, auto* values) {
            return vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, count, values);
        }, "Enumerate surface formats");
        VkSurfaceFormatKHR selected{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        for (auto preferred : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
                               VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
            const auto found = std::find_if(formats.begin(), formats.end(), [preferred](const auto& format) {
                return format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                       (format.format == preferred || format.format == VK_FORMAT_UNDEFINED);
            });
            if (found != formats.end()) { selected = {preferred, found->colorSpace}; break; }
        }
        if (selected.format == VK_FORMAT_UNDEFINED) throw std::runtime_error("No supported 8-bit sRGB presentation format");
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        (faithful_mode ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0);
        if ((capabilities.supportedUsageFlags & usage) != usage)
            throw std::runtime_error("Surface lacks required color/capture/presentation-copy image usage");
        idle();
        // Precise presentation retirement uses maintenance fences where exposed.
        // The unextended fallback follows Khronos' documented WaitIdle approach.
        destroy_swapchain();
        extent = next;
        color_format = selected.format;
        auto create = info<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        create.surface = surface;
        create.minImageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount && create.minImageCount > capabilities.maxImageCount)
            create.minImageCount = capabilities.maxImageCount;
        create.imageFormat = selected.format;
        create.imageColorSpace = selected.colorSpace;
        create.imageExtent = extent;
        create.imageArrayLayers = 1;
        create.imageUsage = usage;
        const std::uint32_t families[] = {graphics_family, present_family};
        create.imageSharingMode = graphics_family == present_family ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
        if (create.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
            create.queueFamilyIndexCount = 2;
            create.pQueueFamilyIndices = families;
        }
        create.preTransform = capabilities.currentTransform;
        for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (capabilities.supportedCompositeAlpha & alpha) { create.compositeAlpha = alpha; break; }
        create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        create.clipped = VK_TRUE;
        const auto result = vkCreateSwapchainKHR(device, &create, nullptr, &swapchain);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { dirty = true; return false; }
        require(result, "Create swapchain");
        const auto handles = enumerate<VkImage>([this](auto* count, auto* values) {
            return vkGetSwapchainImagesKHR(device, swapchain, count, values);
        }, "Get swapchain images");
        images.resize(handles.size());
        auto semaphore = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        for (std::size_t i = 0; i < handles.size(); ++i) {
            images[i].image = handles[i];
            images[i].view = image_view(handles[i], color_format, VK_IMAGE_ASPECT_COLOR_BIT);
            require(vkCreateSemaphore(device, &semaphore, nullptr, &images[i].finished), "Create per-image presentation semaphore");
            if (maintenance) require(vkCreateFence(device, &fence, nullptr, &images[i].present_fence), "Create presentation fence");
        }
        drawable_width = width; drawable_height = height;
        statistics.width = extent.width; statistics.height = extent.height;
        dirty = false;
        return true;
    }

    void image_barrier(VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout,
                       VkImageLayout new_layout, VkPipelineStageFlags2 source_stage,
                       VkAccessFlags2 source_access, VkPipelineStageFlags2 target_stage,
                       VkAccessFlags2 target_access, std::uint32_t mip_levels = 1) {
        auto barrier = info<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
        barrier.srcStageMask = source_stage; barrier.srcAccessMask = source_access;
        barrier.dstStageMask = target_stage; barrier.dstAccessMask = target_access;
        barrier.oldLayout = old_layout; barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {aspect, 0, mip_levels, 0, 1};
        auto dependency = info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    }
    bool draw(const Frame& frame, const std::filesystem::path& capture) {
        if (shutdown_complete) throw std::runtime_error("Cannot draw after renderer shutdown");
        if (acquire_unconsumed)
            throw std::runtime_error("Previous frame failed after acquisition; recreate the renderer before drawing again");
        if (frame.vertices.size() > max_vertices || frame.vertices.size() % 3 != 0)
            throw std::runtime_error("Frame must contain at most 4194304 vertices in complete triangles");
        for (float value : frame.view_projection) if (!std::isfinite(value))
            throw std::runtime_error("Nonfinite view-projection matrix");
        for (const auto& vertex : frame.vertices) {
            for (float value : vertex.position) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite vertex position");
            for (float value : vertex.color) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite vertex color");
        }
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            throw std::runtime_error(std::string("Query drawable: ") + SDL_GetError());
        if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return false;
        if ((dirty || width != drawable_width || height != drawable_height) && !recreate(width, height)) return false;
        if (!depth_image) create_depth();
        if (!pipeline) create_pipeline();
        if (submission_pending) {
            require(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "Wait for previous frame");
            submission_pending = false;
        }
        const auto vertex_bytes = static_cast<VkDeviceSize>(frame.vertices.size()) * sizeof(Vertex);
        if (vertex_bytes) {
            ensure_buffer(vertices, vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            std::memcpy(vertices.mapped, frame.vertices.data(), static_cast<std::size_t>(vertex_bytes));
            host_memory(vertices, false);
        }
        if (!capture.empty()) ensure_buffer(readback, static_cast<VkDeviceSize>(extent.width) * extent.height * 4,
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        std::uint32_t index = 0;
        require(vkResetFences(device, 1, &acquire_fence), "Reset acquire fence");
        const VkResult acquire_result = vkAcquireNextImageKHR(device, swapchain, 1000000000ull, acquired, acquire_fence, &index);
        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) { dirty = true; return false; }
        if (acquire_result == VK_TIMEOUT || acquire_result == VK_NOT_READY) return false;
        if (acquire_result != VK_SUBOPTIMAL_KHR) require(acquire_result, "Acquire swapchain image");
        acquire_unconsumed = true;
        // This also proves the previous presentation of this image completed,
        // and permits safe semaphore destruction if later recording throws.
        require(vkWaitForFences(device, 1, &acquire_fence, VK_TRUE, UINT64_MAX), "Wait for image acquisition");
        auto& image = images.at(index);
        if (maintenance) {
            if (image.present_pending) {
                require(vkWaitForFences(device, 1, &image.present_fence, VK_TRUE, UINT64_MAX), "Wait for previous presentation");
                image.present_pending = false;
            }
            require(vkResetFences(device, 1, &image.present_fence), "Reset presentation fence");
        }
        require(vkResetCommandBuffer(command, 0), "Reset scene command buffer");
        auto begin = info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(vkBeginCommandBuffer(command, &begin), "Begin scene commands");
        // Chain the acquisition semaphore's COLOR_ATTACHMENT_OUTPUT wait
        // through the layout transition; a NONE source stage breaks that
        // execution dependency even though the old contents are discarded.
        image_barrier(image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        const auto depth_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        image_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
                      depth_used ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depth_used ? depth_stages : VK_PIPELINE_STAGE_2_NONE,
                      depth_used ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0,
                      depth_stages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        auto color = info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        color.imageView = image.view; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.008f, 0.012f, 0.02f, 1.0f}};
        if (!srgb(color_format)) for (int channel = 0; channel < 3; ++channel)
            color.clearValue.color.float32[channel] = srgb_channel(color.clearValue.color.float32[channel]);
        auto depth = info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        depth.imageView = depth_view; depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue.depthStencil = {1.0f, 0};
        auto rendering = info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1; rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(command, &rendering);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        struct Push { float matrix[16]; std::uint32_t encode_srgb; } push{};
        static_assert(sizeof(Push) == 68);
        std::copy(frame.view_projection.begin(), frame.view_projection.end(), push.matrix);
        push.encode_srgb = srgb(color_format) ? 0u : 1u;
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        if (vertex_bytes) {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &offset);
            vkCmdDraw(command, static_cast<std::uint32_t>(frame.vertices.size()), 1, 0, 0);
        }
        vkCmdEndRendering(command);
        if (!capture.empty()) {
            image_barrier(image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(command, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &copy);
            auto barrier = info<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = readback.handle; barrier.size = VK_WHOLE_SIZE;
            auto dependency = info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
            dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(command, &dependency);
            image_barrier(image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_2_NONE, 0);
        } else {
            image_barrier(image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
        }
        require(vkEndCommandBuffer(command), "End scene commands");
        auto wait = info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        wait.semaphore = acquired; wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        auto signal = info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signal.semaphore = image.finished; signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        auto commands = info<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        commands.commandBuffer = command;
        auto submit = info<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submit.waitSemaphoreInfoCount = 1; submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1; submit.pCommandBufferInfos = &commands;
        submit.signalSemaphoreInfoCount = 1; submit.pSignalSemaphoreInfos = &signal;
        // Reset only after acquisition and recording succeeded: OUT_OF_DATE
        // leaves the signaled fence reusable rather than deadlocking next draw.
        require(vkResetFences(device, 1, &submitted), "Reset submit fence");
        require(vkQueueSubmit2(graphics, 1, &submit, submitted), "Submit scene");
        submission_pending = true;
        acquire_unconsumed = false;
        depth_used = true;
        auto present_fence = info<VkSwapchainPresentFenceInfoEXT>(VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT);
        present_fence.swapchainCount = 1; present_fence.pFences = &image.present_fence;
        auto presentation = info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        presentation.pNext = maintenance ? &present_fence : nullptr;
        presentation.waitSemaphoreCount = 1; presentation.pWaitSemaphores = &image.finished;
        presentation.swapchainCount = 1; presentation.pSwapchains = &swapchain; presentation.pImageIndices = &index;
        const auto result = vkQueuePresentKHR(present, &presentation);
        if (maintenance && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR))
            image.present_pending = true;
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
            require(result, "Present scene");
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire_result == VK_SUBOPTIMAL_KHR)
            dirty = true;
        if (!capture.empty()) {
            require(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "Wait for screenshot copy");
            submission_pending = false;
            host_memory(readback, true);
            write_png(capture, extent.width, extent.height, static_cast<const unsigned char*>(readback.mapped),
                       color_format == VK_FORMAT_B8G8R8A8_SRGB || color_format == VK_FORMAT_B8G8R8A8_UNORM);
        }
        ++statistics.frames;
        statistics.validation_errors = validation_errors.load(std::memory_order_relaxed);
        return true;
    }

    #include "renderer_faithful.inc"

    void shutdown() {
        if (shutdown_complete) return;
        shutdown_complete = true;
        std::exception_ptr failure;
        if (device) {
            try { idle(); } catch (...) {
                // A device/wait failure must also make the explicit final
                // validation gate fail, while the remaining handles are freed.
                validation_errors.fetch_add(1, std::memory_order_relaxed);
                failure = std::current_exception();
            }
            destroy_swapchain();
            destroy_faithful();
            destroy_buffer(vertices); destroy_buffer(readback);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (acquire_fence) vkDestroyFence(device, acquire_fence, nullptr);
            if (submitted) vkDestroyFence(device, submitted, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
            acquired = VK_NULL_HANDLE;
            acquire_fence = submitted = VK_NULL_HANDLE;
            pool = VK_NULL_HANDLE;
        }
        if (surface) {
            SDL_Vulkan_DestroySurface(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (messenger) {
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(instance, messenger, nullptr);
            messenger = VK_NULL_HANDLE;
        }
        // The debug callback supplied in VkInstanceCreateInfo.pNext also
        // covers vkDestroyInstance. Keep Impl and its counter alive until this
        // returns, then expose the final count through Renderer::stats().
        if (instance) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
        statistics.validation_errors = validation_errors.load(std::memory_order_relaxed);
        if (failure) std::rethrow_exception(failure);
    }
    ~Impl() {
        try { shutdown(); } catch (const std::exception& error) {
            std::fprintf(stderr, "[Vulkan cleanup] %s\n", error.what());
        } catch (...) {
            std::fprintf(stderr, "[Vulkan cleanup] unknown shutdown failure\n");
        }
    }
};

Renderer::Renderer(SDL_Window* window, const std::filesystem::path& shaders, bool validation)
    : impl_(std::make_unique<Impl>()) { impl_->init(window, shaders, validation); }
Renderer::~Renderer() = default;
bool Renderer::draw(const Frame& frame, const std::filesystem::path& capture) { return impl_->draw(frame, capture); }
bool Renderer::draw(const FaithfulFrame& frame, std::vector<std::uint8_t>* rgb) { return impl_->draw_faithful(frame, rgb); }
void Renderer::wait_idle() { impl_->idle(); }
void Renderer::shutdown() { impl_->shutdown(); }
const RenderStats& Renderer::stats() const {
    impl_->statistics.validation_errors = impl_->validation_errors.load(std::memory_order_relaxed);
    return impl_->statistics;
}
} // namespace vt
