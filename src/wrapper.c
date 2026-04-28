#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vulkan_wrapper.h"
#include "device.h"

// Real Vulkan driver handle
static void *real_vk_handle = NULL;
static PFN_vkGetInstanceProcAddr real_vkGetInstanceProcAddr = NULL;

// Forward declarations of our wrapped functions
static VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance_impl(const VkInstanceCreateInfo* pCreateInfo,
                                                           const VkAllocationCallbacks* pAllocator,
                                                           VkInstance* pInstance);
static VKAPI_ATTR void VKAPI_CALL vkDestroyInstance_impl(VkInstance instance,
                                                         const VkAllocationCallbacks* pAllocator);
static VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices_impl(VkInstance instance,
                                                                      uint32_t* pPhysicalDeviceCount,
                                                                      VkPhysicalDevice* pPhysicalDevices);
static VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties_impl(VkPhysicalDevice physicalDevice,
                                                                     VkPhysicalDeviceProperties* pProperties);
static VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2_impl(VkPhysicalDevice physicalDevice,
                                                                    VkPhysicalDeviceFeatures2* pFeatures);
static VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice_impl(VkPhysicalDevice physicalDevice,
                                                          const VkDeviceCreateInfo* pCreateInfo,
                                                          const VkAllocationCallbacks* pAllocator,
                                                          VkDevice* pDevice);
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr_impl(VkDevice device, const char* pName);

// Load the real Vulkan driver
static void load_real_vulkan() {
    if (real_vk_handle) return;
    real_vk_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!real_vk_handle) return;
    real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(real_vk_handle, "vkGetInstanceProcAddr");
}

// Helper to get a real function pointer
static void* get_real_proc(const char* name) {
    if (!real_vk_handle) load_real_vulkan();
    return dlsym(real_vk_handle, name);
}

// ==================== ICD loader interface functions (required) ====================

// Called by the loader to get instance proc addresses
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    // Provide our wrapped functions for known names
    if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)vkCreateInstance_impl;
    if (strcmp(pName, "vkDestroyInstance") == 0) return (PFN_vkVoidFunction)vkDestroyInstance_impl;
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices_impl;
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties_impl;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0) return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2_impl;
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)vkCreateDevice_impl;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr_impl;
    // For other functions, forward to real driver
    if (real_vkGetInstanceProcAddr)
        return real_vkGetInstanceProcAddr(instance, pName);
    return NULL;
}

// Negotiate the ICD loader interface version
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    // We support loader interface version 5 (or whatever the loader asks)
    if (pSupportedVersion) {
        *pSupportedVersion = 5;  // Typical version for modern Vulkan
        return VK_SUCCESS;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

// ==================== Wrapped Vulkan functions ====================

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance_impl(const VkInstanceCreateInfo* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkInstance* pInstance) {
    if (!real_vkCreateInstance)
        real_vkCreateInstance = (PFN_vkCreateInstance)get_real_proc("vkCreateInstance");
    if (!real_vkCreateInstance) return VK_ERROR_INITIALIZATION_FAILED;
    return real_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance_impl(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    if (!real_vkDestroyInstance)
        real_vkDestroyInstance = (PFN_vkDestroyInstance)get_real_proc("vkDestroyInstance");
    if (real_vkDestroyInstance) real_vkDestroyInstance(instance, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices_impl(VkInstance instance,
                                                               uint32_t* pPhysicalDeviceCount,
                                                               VkPhysicalDevice* pPhysicalDevices) {
    if (!real_vkEnumeratePhysicalDevices)
        real_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)get_real_proc("vkEnumeratePhysicalDevices");
    if (!real_vkEnumeratePhysicalDevices) return VK_ERROR_INITIALIZATION_FAILED;
    return real_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties_impl(VkPhysicalDevice physicalDevice,
                                                              VkPhysicalDeviceProperties* pProperties) {
    if (!real_vkGetPhysicalDeviceProperties)
        real_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)get_real_proc("vkGetPhysicalDeviceProperties");
    if (real_vkGetPhysicalDeviceProperties)
        real_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    // Mali workaround: clamp maxStorageBufferRange
    if (pProperties && strstr(pProperties->deviceName, "Mali")) {
        if (pProperties->limits.maxStorageBufferRange > 128 * 1024 * 1024)
            pProperties->limits.maxStorageBufferRange = 128 * 1024 * 1024;
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2_impl(VkPhysicalDevice physicalDevice,
                                                             VkPhysicalDeviceFeatures2* pFeatures) {
    if (!real_vkGetPhysicalDeviceFeatures2)
        real_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)get_real_proc("vkGetPhysicalDeviceFeatures2");
    if (real_vkGetPhysicalDeviceFeatures2)
        real_vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    // Mali workaround: force vertex attribute divisor support
    if (strstr(physicalDevice ? "Mali" : "", "Mali")) {
        pFeatures->features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice_impl(VkPhysicalDevice physicalDevice,
                                                   const VkDeviceCreateInfo* pCreateInfo,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   VkDevice* pDevice) {
    if (!real_vkCreateDevice)
        real_vkCreateDevice = (PFN_vkCreateDevice)get_real_proc("vkCreateDevice");
    if (!real_vkCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;
    return real_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr_impl(VkDevice device, const char* pName) {
    if (!real_vkGetDeviceProcAddr)
        real_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)get_real_proc("vkGetDeviceProcAddr");
    if (real_vkGetDeviceProcAddr)
        return real_vkGetDeviceProcAddr(device, pName);
    return NULL;
}
