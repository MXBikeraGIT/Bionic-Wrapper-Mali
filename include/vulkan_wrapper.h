#ifndef VULKAN_WRAPPER_H
#define VULKAN_WRAPPER_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkInstance* pInstance);
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance,
                                             const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                              const char* pName);
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance,
                                                          uint32_t* pPhysicalDeviceCount,
                                                          VkPhysicalDevice* pPhysicalDevices);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                                         VkPhysicalDeviceProperties* pProperties);
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                                        VkPhysicalDeviceFeatures2* pFeatures);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice,
                                              const VkDeviceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDevice* pDevice);

#ifdef __cplusplus
}
#endif

#endif
