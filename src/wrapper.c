#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ----------------------------------------------------------------------
// Real driver function pointers (loaded from libvulkan.so)
// ----------------------------------------------------------------------
static void *libvulkan = NULL;
static PFN_vkGetInstanceProcAddr real_vkGetInstanceProcAddr = NULL;
static PFN_vkCreateInstance real_vkCreateInstance = NULL;
static PFN_vkDestroyInstance real_vkDestroyInstance = NULL;
static PFN_vkEnumeratePhysicalDevices real_vkEnumeratePhysicalDevices = NULL;
static PFN_vkGetPhysicalDeviceProperties real_vkGetPhysicalDeviceProperties = NULL;
static PFN_vkGetPhysicalDeviceFeatures2 real_vkGetPhysicalDeviceFeatures2 = NULL;
static PFN_vkCreateDevice real_vkCreateDevice = NULL;
static PFN_vkDestroyDevice real_vkDestroyDevice = NULL;
static PFN_vkGetDeviceProcAddr real_vkGetDeviceProcAddr = NULL;

// ----------------------------------------------------------------------
// Helper to load real Vulkan driver
// ----------------------------------------------------------------------
static void load_real_driver(void) {
    if (libvulkan) return;
    libvulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!libvulkan) {
        fprintf(stderr, "Failed to load libvulkan.so\n");
        return;
    }
    real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(libvulkan, "vkGetInstanceProcAddr");
    if (!real_vkGetInstanceProcAddr) {
        fprintf(stderr, "Failed to get vkGetInstanceProcAddr\n");
    }
}

static void* get_real_proc(const char* name) {
    if (!libvulkan) load_real_driver();
    return dlsym(libvulkan, name);
}

// ----------------------------------------------------------------------
// Wrapped instance dispatch – we need to make the loader call us.
// The loader will call vk_icdGetInstanceProcAddr to get our entry points.
// ----------------------------------------------------------------------
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);

// ----------------------------------------------------------------------
// Our implementation of the Vulkan functions (forwarding after workarounds)
// ----------------------------------------------------------------------
static VkResult VKAPI_CALL WrappedCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkInstance* pInstance) {
    load_real_driver();
    if (!real_vkCreateInstance)
        real_vkCreateInstance = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (!real_vkCreateInstance) return VK_ERROR_INITIALIZATION_FAILED;
    return real_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

static void VKAPI_CALL WrappedDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    if (!real_vkDestroyInstance)
        real_vkDestroyInstance = (PFN_vkDestroyInstance)vk_icdGetInstanceProcAddr(NULL, "vkDestroyInstance");
    if (real_vkDestroyInstance) real_vkDestroyInstance(instance, pAllocator);
}

static VkResult VKAPI_CALL WrappedEnumeratePhysicalDevices(VkInstance instance,
                                                          uint32_t* pPhysicalDeviceCount,
                                                          VkPhysicalDevice* pPhysicalDevices) {
    if (!real_vkEnumeratePhysicalDevices)
        real_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vk_icdGetInstanceProcAddr(NULL, "vkEnumeratePhysicalDevices");
    if (!real_vkEnumeratePhysicalDevices) return VK_ERROR_INITIALIZATION_FAILED;
    return real_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

static void VKAPI_CALL WrappedGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                          VkPhysicalDeviceProperties* pProperties) {
    if (!real_vkGetPhysicalDeviceProperties)
        real_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vk_icdGetInstanceProcAddr(NULL, "vkGetPhysicalDeviceProperties");
    if (real_vkGetPhysicalDeviceProperties)
        real_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    // Mali workaround: clamp maxStorageBufferRange
    if (pProperties && strstr(pProperties->deviceName, "Mali")) {
        if (pProperties->limits.maxStorageBufferRange > 128 * 1024 * 1024)
            pProperties->limits.maxStorageBufferRange = 128 * 1024 * 1024;
    }
}

static void VKAPI_CALL WrappedGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                         VkPhysicalDeviceFeatures2* pFeatures) {
    if (!real_vkGetPhysicalDeviceFeatures2)
        real_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)vk_icdGetInstanceProcAddr(NULL, "vkGetPhysicalDeviceFeatures2");
    if (real_vkGetPhysicalDeviceFeatures2)
        real_vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    // Mali workaround: force shaderStorageBufferArrayDynamicIndexing to true
    if (strstr(physicalDevice ? "Mali" : "", "Mali")) {
        pFeatures->features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    }
}

static VkResult VKAPI_CALL WrappedCreateDevice(VkPhysicalDevice physicalDevice,
                                              const VkDeviceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDevice* pDevice) {
    if (!real_vkCreateDevice)
        real_vkCreateDevice = (PFN_vkCreateDevice)vk_icdGetInstanceProcAddr(NULL, "vkCreateDevice");
    if (!real_vkCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;
    return real_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

static void VKAPI_CALL WrappedDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    if (!real_vkDestroyDevice)
        real_vkDestroyDevice = (PFN_vkDestroyDevice)vk_icdGetInstanceProcAddr(NULL, "vkDestroyDevice");
    if (real_vkDestroyDevice) real_vkDestroyDevice(device, pAllocator);
}

static PFN_vkVoidFunction VKAPI_CALL WrappedGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!real_vkGetDeviceProcAddr)
        real_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vk_icdGetInstanceProcAddr(NULL, "vkGetDeviceProcAddr");
    if (real_vkGetDeviceProcAddr)
        return real_vkGetDeviceProcAddr(device, pName);
    return NULL;
}

// ----------------------------------------------------------------------
// ICD entry points (exported)
// ----------------------------------------------------------------------
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!strcmp(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)WrappedCreateInstance;
    if (!strcmp(pName, "vkDestroyInstance")) return (PFN_vkVoidFunction)WrappedDestroyInstance;
    if (!strcmp(pName, "vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)WrappedEnumeratePhysicalDevices;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)WrappedGetPhysicalDeviceProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures2")) return (PFN_vkVoidFunction)WrappedGetPhysicalDeviceFeatures2;
    if (!strcmp(pName, "vkCreateDevice")) return (PFN_vkVoidFunction)WrappedCreateDevice;
    if (!strcmp(pName, "vkDestroyDevice")) return (PFN_vkVoidFunction)WrappedDestroyDevice;
    if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)WrappedGetDeviceProcAddr;
    // For any other function, forward to the real driver
    if (real_vkGetInstanceProcAddr)
        return real_vkGetInstanceProcAddr(instance, pName);
    return NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    if (pSupportedVersion) {
        *pSupportedVersion = 5;
        return VK_SUCCESS;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

// The loader expects the ICD to export vk_icdGetInstanceProcAddr and vk_icdNegotiateLoaderICDInterfaceVersion.
// That's it.
