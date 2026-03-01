cmake_minimum_required(VERSION 3.15)
project(GVulkan LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ddraw SHARED
    src/dllmain.cpp
    src/renderer/vk_window.cpp
    src/renderer/vk_renderer.cpp
    src/renderer/volk/volk.c
    src/proxy/proxy_surface7.cpp
    src/proxy/proxy_device7.cpp
    src/proxy/proxy_d3d7.cpp
    src/proxy/proxy_ddraw7.cpp
    ddraw.def
)

target_include_directories(ddraw PRIVATE src)

target_compile_definitions(ddraw PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
)

target_link_libraries(ddraw PRIVATE
    gdi32
    user32
    dxguid
    comctl32
    dwmapi
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
