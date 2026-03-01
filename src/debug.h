#pragma once

#include <cstdio>
#include <cstdarg>
#include <ddraw.h>
#include <d3d.h>

#ifndef FOURCC_DXT1
#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT2 MAKEFOURCC('D','X','T','2')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT4 MAKEFOURCC('D','X','T','4')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')
#endif

inline void DbgPrint(const char* fmt, ...) {
#ifdef GOPENGL_VERBOSE
    va_list args;
    va_start(args, fmt);
    printf("[GOpenGL] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
#else
    (void)fmt;
#endif
}
