cmake_minimum_required(VERSION 3.15)
project(gvlk LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ddraw SHARED
    dllmain.cpp
    opengl_window.cpp
    ddraw.def
)

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
