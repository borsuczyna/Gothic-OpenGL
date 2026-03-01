#pragma once

#include "VkWindow.h"
#include <cstdint>
#include <ddraw.h>
#include <d3d.h>

struct VkTexHandle {
    VkImage         image     = VK_NULL_HANDLE;
    VmaAllocation   alloc     = VK_NULL_HANDLE;
    VkImageView    view      = VK_NULL_HANDLE;
    VkSampler       sampler   = VK_NULL_HANDLE;
    VkDescriptorSet descSet   = VK_NULL_HANDLE;
    uint32_t        width     = 0;
    uint32_t        height   = 0;
    uint32_t        mipLevels = 1;
    VkFormat        format   = VK_FORMAT_R8G8B8A8_UNORM;
    bool            hasAlpha  = false;
};

namespace VkRenderer {

void Init();
void Shutdown();

void BeginFrame(int windowW, int windowH, int gameW, int gameH);
void EndFrame();
void Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil);

VkTexHandle* UploadTexture(DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt, int totalMipLevels = 1);
void UpdateTexture(VkTexHandle* tex, DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt);
void UploadTextureMipLevel(VkTexHandle* tex, int level, DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt);
void SetTextureMipmapParams(VkTexHandle* tex, int mipCount);
void FreeTexture(VkTexHandle* tex);

void BindTexture(VkTexHandle* tex);
void BindTexture2(VkTexHandle* tex);
void SetAlphaBlendEnabled(bool enabled);
void SetBlendFunc(DWORD srcBlend, DWORD dstBlend);
void SetAlphaTestEnabled(bool enabled);
void SetAlphaRef(DWORD ref);
void SetDepthEnabled(bool enabled);
void SetDepthWriteEnabled(bool enabled);
void SetDepthFunc(DWORD d3dFunc);
void SetStageColorOp(int stage, DWORD op);
void SetStageColorArg(int stage, int argIndex, DWORD value);
void SetStageAlphaOp(int stage, DWORD op);
void SetStageAlphaArg(int stage, int argIndex, DWORD value);
void SetStageTexCoordIndex(int stage, DWORD value);
void SetTextureFactor(DWORD factor);
void SetTextureAddress2U(DWORD d3dAddr);
void SetTextureAddress2V(DWORD d3dAddr);

void SetWorldMatrix(const float* m);
void SetViewMatrix(const float* m);
void SetProjectionMatrix(const float* m);
void SetViewport(DWORD x, DWORD y, DWORD w, DWORD h);
void SetTextureAddressU(DWORD d3dAddr);
void SetTextureAddressV(DWORD d3dAddr);

void DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices, DWORD count);
void DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
                         DWORD vertexCount, const WORD* indices, DWORD indexCount);

} // namespace VkRenderer
