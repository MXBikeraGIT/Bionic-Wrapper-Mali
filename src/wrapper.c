#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ----------------------------------------------------------------------
// Real driver handle and function pointers (loaded from libvulkan.so)
// ----------------------------------------------------------------------
static void *real_vk_handle = NULL;
static PFN_vkGetInstanceProcAddr real_vkGetInstanceProcAddr = NULL;
static PFN_vkCreateInstance real_vkCreateInstance = NULL;
static PFN_vkCreateDevice real_vkCreateDevice = NULL;
static PFN_vkEnumeratePhysicalDevices real_vkEnumeratePhysicalDevices = NULL;
static PFN_vkGetPhysicalDeviceProperties real_vkGetPhysicalDeviceProperties = NULL;
static PFN_vkGetPhysicalDeviceFeatures2 real_vkGetPhysicalDeviceFeatures2 = NULL;
static PFN_vkGetDeviceProcAddr real_vkGetDeviceProcAddr = NULL;
static PFN_vkDestroyInstance real_vkDestroyInstance = NULL;
static PFN_vkDestroyDevice real_vkDestroyDevice = NULL;

// Ensure we load the real driver once
static void load_real_driver(void) {
    if (real_vk_handle) return;
    real_vk_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!real_vk_handle) {
        fprintf(stderr, "ERROR: Failed to load libvulkan.so\n");
        return;
    }
    real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(real_vk_handle, "vkGetInstanceProcAddr");
    // Other functions will be fetched via vkGetInstanceProcAddr later.
}

static void* get_real_proc(const char* name) {
    if (!real_vk_handle) load_real_driver();
    return dlsym(real_vk_handle, name);
}

// ----------------------------------------------------------------------
// Dispatch tables – we need to embed a pointer to the real dispatch table
// inside our instance and device objects. The loader will use this to
// call the correct functions.
// ----------------------------------------------------------------------
typedef struct wrapper_instance_t {
    VkInstance real_instance;
    VkInstance dispatch;   // Actually the loader's dispatch table pointer; we just need to be a valid handle.
} wrapper_instance;

typedef struct wrapper_device_t {
    VkDevice real_device;
    VkDevice dispatch;
} wrapper_device;

// ----------------------------------------------------------------------
// Our implementation of vkGetInstanceProcAddr – the loader uses this to
// query our entry points. It must return function pointers for all
// Vulkan commands that our driver supports.
// ----------------------------------------------------------------------
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_impl(VkInstance instance, const char* pName);

// Forward declarations of our wrapped functions.
static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance, const VkAllocationCallbacks*);
static VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDevices(VkInstance, uint32_t*, VkPhysicalDevice*);
static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties*);
static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2(VkPhysicalDevice, VkPhysicalDeviceFeatures2*);
static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice, const VkAllocationCallbacks*);
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice, const char*);

// The loader will call this function to obtain our function pointers.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!strcmp(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)CreateInstance;
    if (!strcmp(pName, "vkDestroyInstance")) return (PFN_vkVoidFunction)DestroyInstance;
    if (!strcmp(pName, "vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)EnumeratePhysicalDevices;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)GetPhysicalDeviceProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures2")) return (PFN_vkVoidFunction)GetPhysicalDeviceFeatures2;
    if (!strcmp(pName, "vkCreateDevice")) return (PFN_vkVoidFunction)CreateDevice;
    if (!strcmp(pName, "vkDestroyDevice")) return (PFN_vkVoidFunction)DestroyDevice;
    if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)GetDeviceProcAddr;
    // For any other function, if instance is real (non‑null) we could ask the real driver,
    // but the loader expects to get all instance‑function pointers from us.
    // We fallback to our own vkGetInstanceProcAddr_impl for the remaining functions.
    return vkGetInstanceProcAddr_impl(instance, pName);
}

// Negotiate the loader interface version.
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion) {
    if (pVersion) {
        *pVersion = 5;   // We support interface version 5 (common for Vulkan 1.1+)
        return VK_SUCCESS;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

// This is our internal vkGetInstanceProcAddr; it returns our wrapped functions
// for known names, otherwise asks the real driver.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_impl(VkInstance instance, const char* pName) {
    // For the same set we already exported in vk_icdGetInstanceProcAddr, return the same.
    if (!strcmp(pName, "vkCreateInstance")) return (PFN_vkVoidFunction)CreateInstance;
    if (!strcmp(pName, "vkDestroyInstance")) return (PFN_vkVoidFunction)DestroyInstance;
    if (!strcmp(pName, "vkEnumeratePhysicalDevices")) return (PFN_vkVoidFunction)EnumeratePhysicalDevices;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)GetPhysicalDeviceProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures2")) return (PFN_vkVoidFunction)GetPhysicalDeviceFeatures2;
    if (!strcmp(pName, "vkCreateDevice")) return (PFN_vkVoidFunction)CreateDevice;
    if (!strcmp(pName, "vkDestroyDevice")) return (PFN_vkVoidFunction)DestroyDevice;
    if (!strcmp(pName, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)GetDeviceProcAddr;
    // For all other functions, forward to the real driver's vkGetInstanceProcAddr.
    if (real_vkGetInstanceProcAddr)
        return real_vkGetInstanceProcAddr(instance, pName);
    return NULL;
}

// ----------------------------------------------------------------------
// Wrapped functions implementation
// ----------------------------------------------------------------------
VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkInstance* pInstance) {
    load_real_driver();
    if (!real_vkCreateInstance) {
        real_vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr_impl(NULL, "vkCreateInstance");
        if (!real_vkCreateInstance) return VK_ERROR_INITIALIZATION_FAILED;
    }
    // We need to create a real instance and then wrap it.
    VkInstance real_inst;
    VkResult res = real_vkCreateInstance(pCreateInfo, pAllocator, &real_inst);
    if (res != VK_SUCCESS) return res;
    // Allocate our wrapper instance object
    wrapper_instance* wrapper = malloc(sizeof(wrapper_instance));
    if (!wrapper) {
        real_vkDestroyInstance(real_inst, pAllocator);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    wrapper->real_instance = real_inst;
    // The dispatch pointer will be filled by the loader; we set it to the real instance's dispatch table.
    // Actually, the loader expects that the VkInstance handle we return is a pointer that contains
    // a dispatch table at offset 0. The simplest approach: just return the real instance directly,
    // but then the loader would not call our functions for that instance. The correct way is to
    // return a pointer to our wrapper that contains the dispatch table. However, we can cheat:
    // we can store the real instance and override the dispatch table by modifying the VkInstance
    // loader magic. But that's too hairy. Instead, we will not wrap the instance; we will simply
    // return the real instance and let the loader use the real vkGetInstanceProcAddr for it.
    // But then our per-instance functions (DestroyInstance, EnumeratePhysicalDevices) would not be called.
    // We must make our wrapper act as a "layer" that sits between the loader and the real driver.
    // A proper ICD returns a handle that the loader can use to look up functions via
    // vk_icdGetInstanceProcAddr. The loader passes that same handle back to us.
    // So we will return our wrapper pointer, and store the real instance inside it.
    *pInstance = (VkInstance)wrapper;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    wrapper_instance* wrapper = (wrapper_instance*)instance;
    if (wrapper && wrapper->real_instance) {
        if (real_vkDestroyInstance) real_vkDestroyInstance(wrapper->real_instance, pAllocator);
        free(wrapper);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDevices(VkInstance instance,
                                                        uint32_t* pPhysicalDeviceCount,
                                                        VkPhysicalDevice* pPhysicalDevices) {
    wrapper_instance* wrapper = (wrapper_instance*)instance;
    if (!wrapper || !wrapper->real_instance) return VK_ERROR_INITIALIZATION_FAILED;
    if (!real_vkEnumeratePhysicalDevices) {
        real_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr_impl(instance, "vkEnumeratePhysicalDevices");
        if (!real_vkEnumeratePhysicalDevices) return VK_ERROR_INITIALIZATION_FAILED;
    }
    // We need to wrap physical devices as well. The loader expects to get VkPhysicalDevice handles
    // that are opaque to us. We can just return the real ones – the loader will call our
    // GetPhysicalDeviceProperties and other functions with those handles.
    // The loader will use vkGetInstanceProcAddr to resolve functions for those handles,
    // and we will intercept them. So we can return the real physical device handles directly.
    return real_vkEnumeratePhysicalDevices(wrapper->real_instance, pPhysicalDeviceCount, pPhysicalDevices);
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                       VkPhysicalDeviceProperties* pProperties) {
    if (!real_vkGetPhysicalDeviceProperties) {
        real_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr_impl(NULL, "vkGetPhysicalDeviceProperties");
        if (!real_vkGetPhysicalDeviceProperties) return;
    }
    real_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    // Mali workaround: clamp maxStorageBufferRange
    if (pProperties && strstr(pProperties->deviceName, "Mali")) {
        if (pProperties->limits.maxStorageBufferRange > 128 * 1024 * 1024)
            pProperties->limits.maxStorageBufferRange = 128 * 1024 * 1024;
    }
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                      VkPhysicalDeviceFeatures2* pFeatures) {
    if (!real_vkGetPhysicalDeviceFeatures2) {
        real_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr_impl(NULL, "vkGetPhysicalDeviceFeatures2");
        if (!real_vkGetPhysicalDeviceFeatures2) return;
    }
    real_vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    // Mali workaround: force shaderStorageBufferArrayDynamicIndexing to true
    if (strstr(physicalDevice ? "Mali" : "", "Mali")) {
        pFeatures->features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice,
                                            const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDevice* pDevice) {
    if (!real_vkCreateDevice) {
        real_vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr_impl(NULL, "vkCreateDevice");
        if (!real_vkCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDevice real_dev;
    VkResult res = real_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, &real_dev);
    if (res != VK_SUCCESS) return res;
    wrapper_device* wrapper = malloc(sizeof(wrapper_device));
    if (!wrapper) {
        real_vkDestroyDevice(real_dev, pAllocator);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    wrapper->real_device = real_dev;
    *pDevice = (VkDevice)wrapper;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    wrapper_device* wrapper = (wrapper_device*)device;
    if (wrapper && wrapper->real_device) {
        if (real_vkDestroyDevice) real_vkDestroyDevice(wrapper->real_device, pAllocator);
        free(wrapper);
    }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName) {
    // For device functions, we can forward to the real driver's vkGetDeviceProcAddr.
    // However, we need to provide our own wrapped functions for device-specific commands if we intercept them.
    // For now, we just forward everything to the real driver.
    if (!real_vkGetDeviceProcAddr) {
        real_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr_impl(NULL, "vkGetDeviceProcAddr");
        if (!real_vkGetDeviceProcAddr) return NULL;
    }
    wrapper_device* wrapper = (wrapper_device*)device;
    if (wrapper && wrapper->real_device)
        return real_vkGetDeviceProcAddr(wrapper->real_device, pName);
    return NULL;
}
