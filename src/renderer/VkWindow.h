#pragma once

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "volk/volk.h"
#include "vk_mem_alloc.h"
#include <windows.h>

void GVulkan_OnSetWindow(HWND hwnd);
void GVulkan_Stop();
void GVulkan_SetGameResolution(int w, int h);
bool GVulkan_IsReady();

HWND GVulkan_GetHWND();
int  GVulkan_GetWindowWidth();
int  GVulkan_GetWindowHeight();
int  GVulkan_GetGameWidth();
int  GVulkan_GetGameHeight();

VkInstance        GVulkan_GetInstance();
VkDevice          GVulkan_GetDevice();
VkPhysicalDevice  GVulkan_GetPhysicalDevice();
VkRenderPass      GVulkan_GetRenderPass();
VkExtent2D        GVulkan_GetSwapExtent();
VmaAllocator      GVulkan_GetAllocator();
VkQueue           GVulkan_GetGraphicsQueue();
uint32_t          GVulkan_GetGraphicsFamily();
VkFormat          GVulkan_GetDepthFormat();

bool GVulkan_BeginFrame(VkCommandBuffer cmd);
void GVulkan_EndFrame(VkCommandBuffer cmd);
bool GVulkan_NeedsSwapchainRecreate();
void GVulkan_RecreateSwapchain();
