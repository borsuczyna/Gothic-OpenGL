#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "VkWindow.h"
#include "ImGuiManager.h"
#include "../gothic/Gothic.h"
#include <cstdio>
#include <vector>
#include <algorithm>

static HWND  g_gothicHwnd    = nullptr;
static WNDPROC g_origGothicWndProc = nullptr;
static HWND  g_hwnd          = nullptr;
static bool  g_initialized   = false;
static volatile bool g_ready = false;
static volatile bool g_running = false;
static HANDLE g_thread       = nullptr;
static HANDLE g_readyEvent   = nullptr;
static int   g_gameWidth     = 800;
static int   g_gameHeight    = 600;

static const int MAX_FRAMES_IN_FLIGHT = 2;
static const char* WND_CLASS = "GVulkan_WND";

static VkInstance       g_instance       = VK_NULL_HANDLE;
static VkSurfaceKHR    g_surface        = VK_NULL_HANDLE;
static VkPhysicalDevice g_physicalDevice = VK_NULL_HANDLE;
static VkDevice         g_device         = VK_NULL_HANDLE;
static VkQueue          g_graphicsQueue  = VK_NULL_HANDLE;
static VkQueue          g_presentQueue   = VK_NULL_HANDLE;
static uint32_t         g_graphicsFamily = 0;
static uint32_t         g_presentFamily  = 0;
static VmaAllocator     g_allocator      = VK_NULL_HANDLE;

static VkSwapchainKHR   g_swapchain      = VK_NULL_HANDLE;
static VkFormat          g_swapFormat     = VK_FORMAT_B8G8R8A8_UNORM;
static VkExtent2D        g_swapExtent     = {1280, 720};
static std::vector<VkImage>     g_swapImages;
static std::vector<VkImageView> g_swapImageViews;

static VkFormat          g_depthFormat    = VK_FORMAT_D24_UNORM_S8_UINT;
static VkImage           g_depthImage     = VK_NULL_HANDLE;
static VmaAllocation     g_depthAlloc     = VK_NULL_HANDLE;
static VkImageView       g_depthView      = VK_NULL_HANDLE;

static VkRenderPass      g_renderPass     = VK_NULL_HANDLE;
static std::vector<VkFramebuffer> g_framebuffers;

static VkSemaphore       g_imageAvailable[MAX_FRAMES_IN_FLIGHT] = {};
static VkSemaphore       g_renderFinished[MAX_FRAMES_IN_FLIGHT] = {};
static VkFence           g_inFlightFence[MAX_FRAMES_IN_FLIGHT]  = {};
static int               g_currentFrame   = 0;
static uint32_t          g_imageIndex     = 0;
static bool              g_swapchainDirty = false;
static ULONGLONG         g_lastCameraLogMs = 0;

static LRESULT CALLBACK GothicSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SHOWWINDOW:
        if (wp == TRUE) return 0;
        break;
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS* pos = (WINDOWPOS*)lp;
        pos->flags &= ~SWP_SHOWWINDOW;
        pos->flags |= SWP_NOZORDER | SWP_NOACTIVATE;
        break;
    }
    case WM_STYLECHANGING:
    case WM_DISPLAYCHANGE:
        return 0;
    case WM_NCACTIVATE:
        return CallWindowProcA(g_origGothicWndProc, hwnd, msg, FALSE, lp);
    }
    return CallWindowProcA(g_origGothicWndProc, hwnd, msg, wp, lp);
}

static void EnableVisualStyles() {
    static const char manifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
        "  <dependency><dependentAssembly>\r\n"
        "    <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\"\r\n"
        "      version=\"6.0.0.0\" processorArchitecture=\"x86\"\r\n"
        "      publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\r\n"
        "  </dependentAssembly></dependency>\r\n"
        "</assembly>\r\n";

    char tmpPath[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    GetTempFileNameA(tmpPath, "gvk", 0, tmpFile);

    HANDLE hFile = CreateFileA(tmpFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, manifest, sizeof(manifest) - 1, &written, nullptr);
        CloseHandle(hFile);

        ACTCTXA ctx = {};
        ctx.cbSize = sizeof(ctx);
        ctx.lpSource = tmpFile;
        HANDLE hCtx = CreateActCtxA(&ctx);
        if (hCtx != INVALID_HANDLE_VALUE) {
            ULONG_PTR cookie = 0;
            ActivateActCtx(hCtx, &cookie);
        }
        DeleteFileA(tmpFile);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_F11) {
        ImGuiManager::ToggleMenu();
        return 0;
    }

    if (ImGuiManager::HandleWndProc(hwnd, msg, wp, lp))
        return TRUE;

    bool menuOpen = ImGuiManager::IsMenuVisible();

    switch (msg) {
    case WM_CLOSE:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        g_swapchainDirty = true;
        return 0;
    case WM_SETCURSOR:
        if (menuOpen) {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        if (!menuOpen && g_gothicHwnd)
            PostMessageA(g_gothicHwnd, msg, wp, lp);
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
        if (!menuOpen && g_gothicHwnd)
            PostMessageA(g_gothicHwnd, msg, wp, lp);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static VkFormat FindDepthFormat() {
    VkFormat candidates[] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT };
    for (auto fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(g_physicalDevice, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    return VK_FORMAT_D24_UNORM_S8_UINT;
}

static bool CreateVulkanInstance() {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "GVulkan - Gothic II";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "GVulkan";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;

#ifdef GOPENGL_VERBOSE
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = layers;
#endif

    VkResult result = vkCreateInstance(&createInfo, nullptr, &g_instance);
    if (result != VK_SUCCESS) {
        printf("[GVulkan] ERROR: vkCreateInstance failed (%d)\n", result);
        return false;
    }

    volkLoadInstance(g_instance);
    return true;
}

static bool CreateSurface() {
    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = g_hwnd;
    createInfo.hinstance = GetModuleHandleA(nullptr);

    VkResult result = vkCreateWin32SurfaceKHR(g_instance, &createInfo, nullptr, &g_surface);
    if (result != VK_SUCCESS) {
        printf("[GVulkan] ERROR: vkCreateWin32SurfaceKHR failed (%d)\n", result);
        return false;
    }
    return true;
}

static bool SelectPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_instance, &count, nullptr);
    if (count == 0) {
        printf("[GVulkan] ERROR: No Vulkan-capable GPU found\n");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(g_instance, &count, devices.data());

    for (auto& dev : devices) {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, qfProps.data());

        int gfxIdx = -1, presIdx = -1;
        for (uint32_t i = 0; i < qfCount; i++) {
            if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfxIdx = i;
            VkBool32 presSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, g_surface, &presSupport);
            if (presSupport) presIdx = i;
            if (gfxIdx >= 0 && presIdx >= 0) break;
        }

        if (gfxIdx >= 0 && presIdx >= 0) {
            g_physicalDevice = dev;
            g_graphicsFamily = (uint32_t)gfxIdx;
            g_presentFamily  = (uint32_t)presIdx;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            printf("[GVulkan] GPU: %s\n", props.deviceName);
            return true;
        }
    }

    printf("[GVulkan] ERROR: No suitable GPU found\n");
    return false;
}

static bool CreateLogicalDevice() {
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;

    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_graphicsFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    queueInfos.push_back(qci);

    if (g_presentFamily != g_graphicsFamily) {
        qci.queueFamilyIndex = g_presentFamily;
        queueInfos.push_back(qci);
    }

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures features = {};
    features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = (uint32_t)queueInfos.size();
    dci.pQueueCreateInfos = queueInfos.data();
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    dci.pEnabledFeatures = &features;

    VkResult result = vkCreateDevice(g_physicalDevice, &dci, nullptr, &g_device);
    if (result != VK_SUCCESS) {
        printf("[GVulkan] ERROR: vkCreateDevice failed (%d)\n", result);
        return false;
    }

    volkLoadDevice(g_device);
    vkGetDeviceQueue(g_device, g_graphicsFamily, 0, &g_graphicsQueue);
    vkGetDeviceQueue(g_device, g_presentFamily, 0, &g_presentQueue);
    return true;
}

static bool CreateAllocator() {
    VmaVulkanFunctions vmaFuncs = {};
    vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo aci = {};
    aci.vulkanApiVersion = VK_API_VERSION_1_0;
    aci.physicalDevice = g_physicalDevice;
    aci.device = g_device;
    aci.instance = g_instance;
    aci.pVulkanFunctions = &vmaFuncs;

    VkResult result = vmaCreateAllocator(&aci, &g_allocator);
    if (result != VK_SUCCESS) {
        printf("[GVulkan] ERROR: vmaCreateAllocator failed (%d)\n", result);
        return false;
    }
    return true;
}

static void CreateDepthResources() {
    g_depthFormat = FindDepthFormat();

    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = g_depthFormat;
    imgInfo.extent = { g_swapExtent.width, g_swapExtent.height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(g_allocator, &imgInfo, &allocInfo, &g_depthImage, &g_depthAlloc, nullptr);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = g_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = g_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (g_depthFormat == VK_FORMAT_D24_UNORM_S8_UINT || g_depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
        viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(g_device, &viewInfo, nullptr, &g_depthView);
}

static bool CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physicalDevice, g_surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physicalDevice, g_surface, &fmtCount, formats.data());

    g_swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM) {
            colorSpace = fmt.colorSpace;
            break;
        }
    }
    if (!formats.empty() && formats[0].format != VK_FORMAT_UNDEFINED) {
        bool found = false;
        for (auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM) { found = true; break; }
        }
        if (!found) {
            g_swapFormat = formats[0].format;
            colorSpace = formats[0].colorSpace;
        }
    }

    if (caps.currentExtent.width != 0xFFFFFFFF) {
        g_swapExtent = caps.currentExtent;
    } else {
        RECT rc;
        GetClientRect(g_hwnd, &rc);
        g_swapExtent.width = std::max(caps.minImageExtent.width,
            std::min(caps.maxImageExtent.width, (uint32_t)(rc.right - rc.left)));
        g_swapExtent.height = std::max(caps.minImageExtent.height,
            std::min(caps.maxImageExtent.height, (uint32_t)(rc.bottom - rc.top)));
    }

    if (g_swapExtent.width == 0 || g_swapExtent.height == 0) return false;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = g_surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = g_swapFormat;
    sci.imageColorSpace = colorSpace;
    sci.imageExtent = g_swapExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t families[] = { g_graphicsFamily, g_presentFamily };
    if (g_graphicsFamily != g_presentFamily) {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = families;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = g_swapchain;

    VkSwapchainKHR newSwap;
    VkResult result = vkCreateSwapchainKHR(g_device, &sci, nullptr, &newSwap);
    if (result != VK_SUCCESS) {
        printf("[GVulkan] ERROR: vkCreateSwapchainKHR failed (%d)\n", result);
        return false;
    }

    if (g_swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
    g_swapchain = newSwap;

    uint32_t swapCount = 0;
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &swapCount, nullptr);
    g_swapImages.resize(swapCount);
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &swapCount, g_swapImages.data());

    g_swapImageViews.resize(swapCount);
    for (uint32_t i = 0; i < swapCount; i++) {
        VkImageViewCreateInfo ivci = {};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = g_swapImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = g_swapFormat;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;
        vkCreateImageView(g_device, &ivci, nullptr, &g_swapImageViews[i]);
    }

    printf("[GVulkan] Swapchain created %ux%u (%u images)\n",
           g_swapExtent.width, g_swapExtent.height, swapCount);
    return true;
}

static bool CreateRenderPass() {
    VkAttachmentDescription colorAtt = {};
    colorAtt.format = g_swapFormat;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAtt = {};
    depthAtt.format = g_depthFormat;
    depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { colorAtt, depthAtt };

    VkRenderPassCreateInfo rpci = {};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = attachments;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VkResult result = vkCreateRenderPass(g_device, &rpci, nullptr, &g_renderPass);
    if (result != VK_SUCCESS) {
        printf("[GVulkan] ERROR: vkCreateRenderPass failed (%d)\n", result);
        return false;
    }
    return true;
}

static void CreateFramebuffers() {
    g_framebuffers.resize(g_swapImageViews.size());
    for (size_t i = 0; i < g_swapImageViews.size(); i++) {
        VkImageView atts[] = { g_swapImageViews[i], g_depthView };
        VkFramebufferCreateInfo fbci = {};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = g_renderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments = atts;
        fbci.width = g_swapExtent.width;
        fbci.height = g_swapExtent.height;
        fbci.layers = 1;
        vkCreateFramebuffer(g_device, &fbci, nullptr, &g_framebuffers[i]);
    }
}

static void CreateSyncObjects() {
    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(g_device, &sci, nullptr, &g_imageAvailable[i]);
        vkCreateSemaphore(g_device, &sci, nullptr, &g_renderFinished[i]);
        vkCreateFence(g_device, &fci, nullptr, &g_inFlightFence[i]);
    }
}

static void CleanupSwapchain() {
    for (auto fb : g_framebuffers) vkDestroyFramebuffer(g_device, fb, nullptr);
    g_framebuffers.clear();
    if (g_depthView) { vkDestroyImageView(g_device, g_depthView, nullptr); g_depthView = VK_NULL_HANDLE; }
    if (g_depthImage) { vmaDestroyImage(g_allocator, g_depthImage, g_depthAlloc); g_depthImage = VK_NULL_HANDLE; }
    for (auto iv : g_swapImageViews) vkDestroyImageView(g_device, iv, nullptr);
    g_swapImageViews.clear();
}

static bool InitVulkan() {
    VkResult result = volkInitialize();
    if (result != VK_SUCCESS) {
        printf("[GVulkan] ERROR: volkInitialize failed (%d) - Vulkan not available\n", result);
        return false;
    }

    if (!CreateVulkanInstance()) return false;
    if (!CreateSurface()) return false;
    if (!SelectPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;
    if (!CreateAllocator()) return false;

    g_depthFormat = FindDepthFormat();

    if (!CreateSwapchain()) return false;
    if (!CreateRenderPass()) return false;
    CreateDepthResources();
    CreateFramebuffers();
    CreateSyncObjects();

    printf("[GVulkan] Vulkan initialized successfully\n");
    fflush(stdout);
    return true;
}

static DWORD WINAPI VkThreadProc(LPVOID) {
    EnableVisualStyles();

    HINSTANCE hInst = GetModuleHandleA(nullptr);

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName  = WND_CLASS;
    RegisterClassExA(&wc);

    RECT r = {0, 0, 1280, 720};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExA(
        0, WND_CLASS, "GVulkan - Gothic Vulkan",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) {
        printf("[GVulkan] ERROR: Failed to create window\n");
        fflush(stdout);
        SetEvent(g_readyEvent);
        return 1;
    }

    bool ok = InitVulkan();
    g_running = ok;
    g_ready = ok;
    SetEvent(g_readyEvent);

    if (!ok) return 1;

    while (g_running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(1);
    }

    HINSTANCE hI = GetModuleHandleA(nullptr);
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
    UnregisterClassA(WND_CLASS, hI);
    return 0;
}

void GVulkan_OnSetWindow(HWND hwnd) {
    if (g_initialized || !hwnd) return;
    g_initialized = true;
    g_gothicHwnd = hwnd;

    printf("[GVulkan] Captured Gothic HWND=0x%p, hiding it\n", hwnd);
    fflush(stdout);

    g_origGothicWndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)GothicSubclassProc);
    ShowWindow(hwnd, SW_HIDE);

    g_readyEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, VkThreadProc, nullptr, 0, nullptr);

    WaitForSingleObject(g_readyEvent, 10000);
    CloseHandle(g_readyEvent);
    g_readyEvent = nullptr;

    if (g_ready) {
        printf("[GVulkan] Vulkan thread ready\n");
        fflush(stdout);
    }
}

bool GVulkan_BeginFrame(VkCommandBuffer cmd) {
    if (!g_ready || !g_device) return false;

    ULONGLONG nowMs = GetTickCount64();
    if (nowMs - g_lastCameraLogMs >= 1000) {
        g_lastCameraLogMs = nowMs;
        if (Gothic::Game::Get().IsInGame()) {
            auto camPos = Gothic::Game::GetCameraVobPosition();
            auto camInfo = Gothic::Game::GetCameraInfo();
            if (camPos.valid && camInfo.valid) {
                printf("[GVulkan] Camera: x=%.2f y=%.2f z=%.2f fovH=%.2f fovV=%.2f\n",
                       camPos.pos.x, camPos.pos.y, camPos.pos.z, camInfo.fovH, camInfo.fovV);
                fflush(stdout);
            }
        }
    }

    vkWaitForFences(g_device, 1, &g_inFlightFence[g_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX,
        g_imageAvailable[g_currentFrame], VK_NULL_HANDLE, &g_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        g_swapchainDirty = true;
        return false;
    }

    vkResetFences(g_device, 1, &g_inFlightFence[g_currentFrame]);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkRenderPassBeginInfo rpBegin = {};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = g_renderPass;
    rpBegin.framebuffer = g_framebuffers[g_imageIndex];
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = g_swapExtent;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

void GVulkan_EndFrame(VkCommandBuffer cmd) {
    if (!g_ready || !g_device) return;

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSemaphore waitSems[] = { g_imageAvailable[g_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSems[] = { g_renderFinished[g_currentFrame] };

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSems;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSems;

    vkQueueSubmit(g_graphicsQueue, 1, &submitInfo, g_inFlightFence[g_currentFrame]);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSems;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &g_swapchain;
    presentInfo.pImageIndices = &g_imageIndex;

    VkResult result = vkQueuePresentKHR(g_presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        g_swapchainDirty = true;

    g_currentFrame = (g_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

bool GVulkan_NeedsSwapchainRecreate() { return g_swapchainDirty; }

void GVulkan_RecreateSwapchain() {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    if (rc.right - rc.left == 0 || rc.bottom - rc.top == 0) return;

    vkDeviceWaitIdle(g_device);
    CleanupSwapchain();
    CreateSwapchain();
    CreateDepthResources();
    CreateFramebuffers();
    g_swapchainDirty = false;
}

void GVulkan_Stop() {
    if (!g_running && !g_device) return;

    g_running = false;

    if (g_device) {
        vkDeviceWaitIdle(g_device);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (g_imageAvailable[i]) vkDestroySemaphore(g_device, g_imageAvailable[i], nullptr);
            if (g_renderFinished[i]) vkDestroySemaphore(g_device, g_renderFinished[i], nullptr);
            if (g_inFlightFence[i])  vkDestroyFence(g_device, g_inFlightFence[i], nullptr);
        }

        CleanupSwapchain();
        if (g_renderPass) vkDestroyRenderPass(g_device, g_renderPass, nullptr);
        if (g_swapchain) vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
        if (g_allocator) vmaDestroyAllocator(g_allocator);
        vkDestroyDevice(g_device, nullptr);
    }
    if (g_surface) vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
    if (g_instance) vkDestroyInstance(g_instance, nullptr);

    if (g_thread) {
        PostThreadMessageA(GetThreadId(g_thread), WM_QUIT, 0, 0);
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }

    g_device = VK_NULL_HANDLE;
    g_instance = VK_NULL_HANDLE;
    g_initialized = false;
    g_ready = false;
}

void GVulkan_SetGameResolution(int w, int h) {
    g_gameWidth = w;
    g_gameHeight = h;
    printf("[GVulkan] Game resolution set to %dx%d\n", w, h);
    fflush(stdout);
}

bool GVulkan_IsReady() { return g_ready; }
HWND GVulkan_GetHWND() { return g_hwnd; }

int GVulkan_GetWindowWidth() {
    if (!g_hwnd) return 800;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    return rc.right - rc.left;
}

int GVulkan_GetWindowHeight() {
    if (!g_hwnd) return 600;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    return rc.bottom - rc.top;
}

int  GVulkan_GetGameWidth()    { return g_gameWidth; }
int  GVulkan_GetGameHeight()   { return g_gameHeight; }

VkInstance       GVulkan_GetInstance()       { return g_instance; }
VkDevice         GVulkan_GetDevice()         { return g_device; }
VkPhysicalDevice GVulkan_GetPhysicalDevice() { return g_physicalDevice; }
VkRenderPass     GVulkan_GetRenderPass()     { return g_renderPass; }
VkExtent2D       GVulkan_GetSwapExtent()     { return g_swapExtent; }
VmaAllocator     GVulkan_GetAllocator()      { return g_allocator; }
VkQueue          GVulkan_GetGraphicsQueue()  { return g_graphicsQueue; }
uint32_t         GVulkan_GetGraphicsFamily() { return g_graphicsFamily; }
VkFormat         GVulkan_GetDepthFormat()    { return g_depthFormat; }
