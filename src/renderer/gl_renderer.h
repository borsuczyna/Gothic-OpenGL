#pragma once

#include <windows.h>
#include <GL/gl.h>
#include <ddraw.h>
#include <d3d.h>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

#ifndef GL_LINEAR_MIPMAP_NEAREST
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#endif

namespace GLRenderer {

void Init();
void Shutdown();

void BeginFrame(int windowW, int windowH, int gameW, int gameH);
void Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil);

GLuint UploadTexture(DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt);
void   UpdateTexture(GLuint texId, DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt);
void   UploadTextureMipLevel(GLuint texId, int level, DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt);
void   SetTextureMipmapParams(GLuint texId, int mipCount);
void   FreeTexture(GLuint texId);

void BindTexture(GLuint texId);
void SetAlphaBlendEnabled(bool enabled);
void SetBlendFunc(DWORD srcBlend, DWORD dstBlend);
void SetAlphaTestEnabled(bool enabled);
void SetAlphaRef(DWORD ref);
void SetDepthEnabled(bool enabled);
void SetDepthWriteEnabled(bool enabled);
void SetDepthFunc(DWORD d3dFunc);

void SetWorldMatrix(const float* m);
void SetViewMatrix(const float* m);
void SetProjectionMatrix(const float* m);
void SetViewport(DWORD x, DWORD y, DWORD w, DWORD h);
void SetTextureAddressU(DWORD d3dAddr);
void SetTextureAddressV(DWORD d3dAddr);

void DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices, DWORD count);
void DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
                          DWORD vertexCount, const WORD* indices, DWORD indexCount);

}
