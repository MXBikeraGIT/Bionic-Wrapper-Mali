#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

// Real driver handle
static void *libvulkan = NULL;
static PFN_vkGetInstanceProcAddr real_vkGetInstanceProcAddr = NULL;

// Helper to load real driver and get function pointers
static void load_real(void) {
    if (libvulkan) return;
    libvulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!libvulkan) return;
    real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(libvulkan, "vkGetInstanceProcAddr");
}

// Forward a call to real driver by name (used inside wrapped functions)
#define FORWARD(instance, name, ...) \
    do { \
        if (!real_vkGetInstanceProcAddr) load_real(); \
        PFN_vkVoidFunction fp = real_vkGetInstanceProcAddr(instance, #name); \
        if (!fp) return; \
        ((PFN_##name)fp)(__VA_ARGS__); \
    } while(0)

// For functions that return VkResult
#define FORWARD_RESULT(instance, name, ...) \
    do { \
        if (!real_vkGetInstanceProcAddr) load_real(); \
        PFN_vkVoidFunction fp = real_vkGetInstanceProcAddr(instance, #name); \
        if (!fp) return VK_ERROR_INITIALIZATION_FAILED; \
        return ((PFN_##name)fp)(__VA_ARGS__); \
    } while(0)

// Helper to get real function pointer for device functions
static PFN_vkVoidFunction get_device_proc(VkDevice device, const char* name) {
    if (!real_vkGetInstanceProcAddr) load_real();
    return real_vkGetInstanceProcAddr((VkInstance)device, name);
}

// ----------------------------------------------------------------------
// Wrapped instance functions
// ----------------------------------------------------------------------
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkInstance* pInstance) {
    load_real();
    PFN_vkCreateInstance real = (PFN_vkCreateInstance)real_vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (!real) return VK_ERROR_INITIALIZATION_FAILED;
    return real(pCreateInfo, pAllocator, pInstance);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    FORWARD(instance, vkDestroyInstance, instance, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance,
                                                          uint32_t* pPhysicalDeviceCount,
                                                          VkPhysicalDevice* pPhysicalDevices) {
    FORWARD_RESULT(instance, vkEnumeratePhysicalDevices, instance, pPhysicalDeviceCount, pPhysicalDevices);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                         VkPhysicalDeviceProperties* pProperties) {
    // Forward first
    FORWARD((VkInstance)physicalDevice, vkGetPhysicalDeviceProperties, physicalDevice, pProperties);
    // Mali workaround: clamp maxStorageBufferRange
    if (pProperties && strstr(pProperties->deviceName, "Mali")) {
        if (pProperties->limits.maxStorageBufferRange > 128 * 1024 * 1024)
            pProperties->limits.maxStorageBufferRange = 128 * 1024 * 1024;
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                        VkPhysicalDeviceFeatures2* pFeatures) {
    FORWARD((VkInstance)physicalDevice, vkGetPhysicalDeviceFeatures2, physicalDevice, pFeatures);
    // Mali workaround: force shaderStorageBufferArrayDynamicIndexing
    if (pFeatures && strstr(physicalDevice ? "Mali" : "", "Mali")) {
        pFeatures->features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice,
                                              const VkDeviceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDevice* pDevice) {
    FORWARD_RESULT((VkInstance)physicalDevice, vkCreateDevice, physicalDevice, pCreateInfo, pAllocator, pDevice);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    FORWARD((VkInstance)device, vkDestroyDevice, device, pAllocator);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    // Return our own device function pointers if we wrap any; otherwise forward to real driver.
    // For simplicity, we forward everything to the real driver's vkGetDeviceProcAddr.
    return get_device_proc(device, pName);
}

// ----------------------------------------------------------------------
// ICD loader interface (required)
// ----------------------------------------------------------------------
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    // Return our wrapped functions for known names
    if (!strcmp(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)vkCreateInstance;
    if (!strcmp(pName, "vkDestroyInstance")) return (PFN_vkVoidFunction)vkDestroyInstance;
    if (!strcmp(pName, "vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures2")) return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2;
    if (!strcmp(pName, "vkCreateDevice")) return (PFN_vkVoidFunction)vkCreateDevice;
    if (!strcmp(pName, "vkDestroyDevice")) return (PFN_vkVoidFunction)vkDestroyDevice;
    if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    // For all other functions, let the loader get them from the real driver
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
