cmake_minimum_required(VERSION 3.15)
project(GOpenGL LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ddraw SHARED
    src/dllmain.cpp
    src/renderer/gl_renderer.cpp
    src/renderer/gl_window.cpp
    src/proxy/proxy_surface7.cpp
    src/proxy/proxy_device7.cpp
    src/proxy/proxy_d3d7.cpp
    src/proxy/proxy_ddraw7.cpp
    ddraw.def
)

target_include_directories(ddraw PRIVATE src)

target_link_libraries(ddraw PRIVATE
    opengl32
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
