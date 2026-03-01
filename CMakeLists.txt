cmake_minimum_required(VERSION 3.15)
project(GVulkan LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ddraw SHARED
    src/dllmain.cpp
    src/renderer/VkWindow.cpp
    src/renderer/VkRenderer.cpp
    src/renderer/ImGuiManager.cpp
    src/renderer/Pipeline.cpp
    src/renderer/TextureUtils.cpp
    src/renderer/TimeCycle.cpp
    src/renderer/volk/volk.c
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/backends/imgui_impl_vulkan.cpp
    src/imgui/backends/imgui_impl_win32.cpp
    src/proxy/ProxySurface7.cpp
    src/proxy/ProxyDevice7.cpp
    src/proxy/ProxyD3d7.cpp
    src/proxy/ProxyDdraw7.cpp
    ddraw.def
)

target_include_directories(ddraw PRIVATE src src/imgui)

target_compile_definitions(ddraw PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
)

target_link_libraries(ddraw PRIVATE
    gdi32
    user32
    dxguid
    comctl32
    dwmapi
    imm32
)

set_target_properties(ddraw PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ddraw"
)

target_link_options(ddraw PRIVATE
    -Wl,--enable-stdcall-fixup
    -static-libgcc
    -static-libstdc++
    -static
)
