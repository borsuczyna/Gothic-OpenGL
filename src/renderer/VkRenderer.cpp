#include "VkRenderer.h"
#include "ImGuiManager.h"
#include "TimeCycle.h"
#include "Types.h"
#include "Pipeline.h"
#include "TextureUtils.h"
#include "WorldReconstructor.h"
#include "../shaders/GothicShaders.h"
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <unordered_map>

using namespace gvlk;

namespace VkRenderer {

static bool s_initialized = false;
static bool s_frameActive = false;
static int  s_windowW = 800, s_windowH = 600;
static int  s_gameW   = 800, s_gameH   = 600;

static float s_worldMatrix[16];
static float s_viewMatrix[16];
static float s_projMatrix[16];
static bool  s_in2DMode = true;

static DWORD s_vpX = 0, s_vpY = 0, s_vpW = 800, s_vpH = 600;

static bool  s_blendEnabled     = false;
static DWORD s_srcBlend         = D3DBLEND_ONE;
static DWORD s_dstBlend         = D3DBLEND_ZERO;
static bool  s_alphaTestEnabled = false;
static DWORD s_alphaRef         = 0;
static bool  s_depthEnabled     = true;
static bool  s_depthWriteEnabled = true;
static DWORD s_depthFunc        = D3DCMP_LESSEQUAL;

static VkTexHandle* s_boundTexture = nullptr;
static VkTexHandle* s_boundTexture2 = nullptr;
static DWORD s_stage0ColorOp = 4; // D3DTOP_MODULATE
static DWORD s_stage1ColorOp = 1; // D3DTOP_DISABLE
static DWORD s_stage0Arg1 = 2;    // D3DTA_TEXTURE
static DWORD s_stage0Arg2 = 0;    // D3DTA_DIFFUSE
static DWORD s_stage1Arg1 = 2;    // D3DTA_TEXTURE
static DWORD s_stage1Arg2 = 1;    // D3DTA_CURRENT
static DWORD s_stage0AlphaOp   = 2; // D3DTOP_SELECTARG1
static DWORD s_stage0AlphaArg1 = 2; // D3DTA_TEXTURE
static DWORD s_stage0AlphaArg2 = 1; // D3DTA_CURRENT
static DWORD s_textureFactor   = 0xFFFFFFFF;
static DWORD s_stage1AddrU = D3DTADDRESS_WRAP;
static DWORD s_stage1AddrV = D3DTADDRESS_WRAP;
static DWORD s_stage0TexCoordIdx = 0;
static DWORD s_stage1TexCoordIdx = 0;

static VkCommandPool   s_cmdPool = VK_NULL_HANDLE;
static const int MAX_FRAMES = 2;
static VkCommandBuffer s_cmdBufs[MAX_FRAMES] = {};
static int             s_frameIndex = 0;

static const uint32_t VERTEX_BUFFER_SIZE = 16 * 1024 * 1024;
static const uint32_t MAX_VERTICES = VERTEX_BUFFER_SIZE / sizeof(GVertex);
static const uint32_t INDEX_BUFFER_SIZE = 4 * 1024 * 1024;
static const uint32_t MAX_INDICES = INDEX_BUFFER_SIZE / sizeof(uint16_t);

static VkBuffer       s_vertexBuffer[MAX_FRAMES]  = {};
static VmaAllocation  s_vertexAlloc[MAX_FRAMES]    = {};
static GVertex*       s_vertexMapped[MAX_FRAMES]   = {};

static VkBuffer       s_indexBuffer[MAX_FRAMES]    = {};
static VmaAllocation  s_indexAlloc[MAX_FRAMES]     = {};
static uint16_t*      s_indexMapped[MAX_FRAMES]    = {};

static uint32_t       s_vertexOffset    = 0;
static uint32_t       s_indexOffset     = 0;

static GVertex*       s_curVerts        = nullptr;
static uint16_t*      s_curIdx          = nullptr;
static VkBuffer       s_curVertBuf      = VK_NULL_HANDLE;
static VkBuffer       s_curIdxBuf       = VK_NULL_HANDLE;

static VkBuffer       s_stagingBuffer   = VK_NULL_HANDLE;
static VmaAllocation  s_stagingAlloc    = VK_NULL_HANDLE;
static void*          s_stagingMapped   = nullptr;
static const uint32_t STAGING_BUFFER_SIZE = 16 * 1024 * 1024;

static VkDescriptorSetLayout s_descSetLayout = VK_NULL_HANDLE;
static VkPipelineLayout      s_pipelineLayout = VK_NULL_HANDLE;
static VkDescriptorPool      s_descPool       = VK_NULL_HANDLE;

static PipelineCache s_pipelineCache;

static VkShaderModule s_vertModule = VK_NULL_HANDLE;
static VkShaderModule s_fragModule = VK_NULL_HANDLE;

static VkTexHandle    s_whiteTex;
static VkSampler      s_defaultSampler = VK_NULL_HANDLE;

static gvlk::TimeCycle s_timeCycle;

static VkSampler      s_currentSamplerU = VK_NULL_HANDLE;
static VkSampler      s_currentSamplerV = VK_NULL_HANDLE;
static DWORD          s_addressU = D3DTADDRESS_WRAP;
static DWORD          s_addressV = D3DTADDRESS_WRAP;

static void MakeIdentity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void MatMul(float* out, const float* a, const float* b) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = 0;
            for (int k = 0; k < 4; k++)
                out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
        }
}

static void Transpose(float* out, const float* in) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[c * 4 + r] = in[r * 4 + c];
}

static VkShaderModule CreateShaderModule(const uint32_t* code, size_t sizeBytes) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = sizeBytes;
    ci.pCode = code;
    VkShaderModule mod;
    vkCreateShaderModule(GVulkan_GetDevice(), &ci, nullptr, &mod);
    return mod;
}

static VkCompareOp D3DCmpToVk(DWORD func) {
    switch (func) {
    case D3DCMP_NEVER:        return VK_COMPARE_OP_NEVER;
    case D3DCMP_LESS:         return VK_COMPARE_OP_LESS;
    case D3DCMP_EQUAL:        return VK_COMPARE_OP_EQUAL;
    case D3DCMP_LESSEQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case D3DCMP_GREATER:      return VK_COMPARE_OP_GREATER;
    case D3DCMP_NOTEQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
    case D3DCMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case D3DCMP_ALWAYS:       return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

static VkBlendFactor D3DBlendToVk(DWORD blend) {
    switch (blend) {
    case D3DBLEND_ZERO:         return VK_BLEND_FACTOR_ZERO;
    case D3DBLEND_ONE:          return VK_BLEND_FACTOR_ONE;
    case D3DBLEND_SRCCOLOR:     return VK_BLEND_FACTOR_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA:     return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA:    return VK_BLEND_FACTOR_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR:    return VK_BLEND_FACTOR_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

static VkPipeline GetOrCreatePipeline(const PipelineKey& key) {
    return s_pipelineCache.getOrCreate(key, GVulkan_GetDevice(), GVulkan_GetRenderPass(),
                                      s_pipelineLayout, s_vertModule, s_fragModule);
}

static void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage,
                         VkBuffer& buffer, VmaAllocation& alloc, void** mapped) {
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci = {};
    aci.usage = memUsage;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo = {};
    vmaCreateBuffer(GVulkan_GetAllocator(), &bci, &aci, &buffer, &alloc, &allocInfo);
    if (mapped) *mapped = allocInfo.pMappedData;
}

static VkCommandBuffer BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = s_cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(GVulkan_GetDevice(), &ai, &cmd);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

static void EndSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(GVulkan_GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(GVulkan_GetGraphicsQueue());
    vkFreeCommandBuffers(GVulkan_GetDevice(), s_cmdPool, 1, &cmd);
}

static uint32_t CalcMipLevels(uint32_t w, uint32_t h) {
    uint32_t levels = 1;
    uint32_t dim = w > h ? w : h;
    while (dim > 1) { dim >>= 1; levels++; }
    return levels;
}

static void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                                  VkImageLayout newLayout, uint32_t baseMip, uint32_t mipCount) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = mipCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static unsigned char ExtractChannel(DWORD pixel, DWORD mask) {
    if (mask == 0) return 255;
    int shift = 0;
    DWORD m = mask;
    while (m && !(m & 1)) { shift++; m >>= 1; }
    int bits = 0;
    while (m & 1) { bits++; m >>= 1; }
    if (bits == 0) return 255;
    DWORD value = (pixel & mask) >> shift;
    return (unsigned char)(value * 255 / ((1 << bits) - 1));
}

static DWORD BppFromMasks(const DDPIXELFORMAT& pf) {
    DWORD allMasks = pf.dwRBitMask | pf.dwGBitMask | pf.dwBBitMask | pf.dwRGBAlphaBitMask;
    if (allMasks == 0) return 0;
    int highBit = 0;
    while (allMasks >> highBit) highBit++;
    return (highBit <= 8) ? 8 : (highBit <= 16) ? 16 : 32;
}

static void ConvertToRGBA(const void* src, std::vector<unsigned char>& dst,
                          DWORD w, DWORD h, DWORD pitch, const DDPIXELFORMAT& fmt) {
    dst.resize(w * h * 4);
    DWORD bpp = fmt.dwRGBBitCount;
    if (bpp == 0) {
        bpp = BppFromMasks(fmt);
        if (bpp == 0) bpp = 32;
    }
    for (DWORD y = 0; y < h; y++) {
        const unsigned char* row = (const unsigned char*)src + y * pitch;
        for (DWORD x = 0; x < w; x++) {
            DWORD pixel = 0;
            if (bpp == 32)      pixel = *(const DWORD*)(row + x * 4);
            else if (bpp == 16) pixel = *(const unsigned short*)(row + x * 2);
            else if (bpp == 24) pixel = row[x*3] | (row[x*3+1] << 8) | (row[x*3+2] << 16);
            DWORD idx = (y * w + x) * 4;
            dst[idx + 0] = ExtractChannel(pixel, fmt.dwRBitMask);
            dst[idx + 1] = ExtractChannel(pixel, fmt.dwGBitMask);
            dst[idx + 2] = ExtractChannel(pixel, fmt.dwBBitMask);
            dst[idx + 3] = (fmt.dwFlags & DDPF_ALPHAPIXELS) ?
                ExtractChannel(pixel, fmt.dwRGBAlphaBitMask) : 255;
        }
    }
}

static VkFormat FourCCToVkFormat(DWORD fourCC) {
    switch (fourCC) {
    case MAKEFOURCC('D','X','T','1'): return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case MAKEFOURCC('D','X','T','3'): return VK_FORMAT_BC2_UNORM_BLOCK;
    case MAKEFOURCC('D','X','T','5'): return VK_FORMAT_BC3_UNORM_BLOCK;
    default: return VK_FORMAT_UNDEFINED;
    }
}

static DWORD FourCCBlockSize(DWORD fourCC) {
    if (fourCC == MAKEFOURCC('D','X','T','1')) return 8;
    return 16;
}

static void UploadImageData(VkImage image, DWORD w, DWORD h, uint32_t mipLevel,
                            const void* data, DWORD dataSize) {
    if (dataSize > STAGING_BUFFER_SIZE) return;
    memcpy(s_stagingMapped, data, dataSize);

    VkCommandBuffer cmd = BeginSingleTimeCommands();
    TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel, 1);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { w, h, 1 };
    vkCmdCopyBufferToImage(cmd, s_stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevel, 1);
    EndSingleTimeCommands(cmd);
}

static void GenerateMipmaps(VkImage image, uint32_t w, uint32_t h,
                            uint32_t mipLevels, uint32_t fromLevel) {
    if (fromLevel + 1 >= mipLevels) return;

    VkCommandBuffer cmd = BeginSingleTimeCommands();

    int32_t mipW = (int32_t)(w >> fromLevel);
    int32_t mipH = (int32_t)(h >> fromLevel);
    if (mipW < 1) mipW = 1;
    if (mipH < 1) mipH = 1;

    for (uint32_t i = fromLevel; i < mipLevels - 1; i++) {
        TransitionImageLayout(cmd, image,
            (i == fromLevel) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                             : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i, 1);

        TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, i + 1, 1);

        int32_t nextW = mipW > 1 ? mipW / 2 : 1;
        int32_t nextH = mipH > 1 ? mipH / 2 : 1;

        VkImageBlit blit = {};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipW, mipH, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i + 1, 0, 1 };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextW, nextH, 1 };

        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i, 1);

        mipW = nextW;
        mipH = nextH;
    }

    TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels - 1, 1);

    EndSingleTimeCommands(cmd);
}

static VkSampler CreateSampler(uint32_t mipLevels) {
    VkSamplerCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = mipLevels > 1 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod = (float)mipLevels;
    if (mipLevels > 1) {
        sci.anisotropyEnable = VK_TRUE;
        sci.maxAnisotropy = 16.0f;
    } else {
        sci.maxAnisotropy = 1.0f;
    }
    VkSampler sampler;
    vkCreateSampler(GVulkan_GetDevice(), &sci, nullptr, &sampler);
    return sampler;
}

static VkDescriptorSet AllocateDescriptorSet() {
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = s_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &s_descSetLayout;
    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(GVulkan_GetDevice(), &ai, &set);
    if (result != VK_SUCCESS) return VK_NULL_HANDLE;
    return set;
}

static void UpdateDescriptorSet(VkDescriptorSet set, VkImageView view, VkSampler sampler) {
    VkDescriptorImageInfo imgInfo = {};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView = view;
    imgInfo.sampler = sampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(GVulkan_GetDevice(), 1, &write, 0, nullptr);
}

static void CreateWhiteTexture() {
    VkDevice device = GVulkan_GetDevice();
    uint32_t white = 0xFFFFFFFF;

    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {1, 1, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(GVulkan_GetAllocator(), &ici, &aci, &s_whiteTex.image, &s_whiteTex.alloc, nullptr);

    UploadImageData(s_whiteTex.image, 1, 1, 0, &white, 4);

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = s_whiteTex.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(device, &vci, nullptr, &s_whiteTex.view);

    s_whiteTex.sampler = CreateSampler(1);
    s_whiteTex.descSet = AllocateDescriptorSet();
    UpdateDescriptorSet(s_whiteTex.descSet, s_whiteTex.view, s_whiteTex.sampler);
    s_whiteTex.width = 1;
    s_whiteTex.height = 1;
    s_whiteTex.mipLevels = 1;
}

void Init() {
    if (s_initialized) return;
    VkDevice device = GVulkan_GetDevice();

    VkCommandPoolCreateInfo cpci = {};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = GVulkan_GetGraphicsFamily();
    vkCreateCommandPool(device, &cpci, nullptr, &s_cmdPool);

    VkCommandBufferAllocateInfo cbai = {};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = s_cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = MAX_FRAMES;
    vkAllocateCommandBuffers(device, &cbai, s_cmdBufs);

    for (int i = 0; i < MAX_FRAMES; i++) {
        CreateBuffer(VERTEX_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VMA_MEMORY_USAGE_CPU_TO_GPU, s_vertexBuffer[i], s_vertexAlloc[i], (void**)&s_vertexMapped[i]);
        CreateBuffer(INDEX_BUFFER_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VMA_MEMORY_USAGE_CPU_TO_GPU, s_indexBuffer[i], s_indexAlloc[i], (void**)&s_indexMapped[i]);
    }
    CreateBuffer(STAGING_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VMA_MEMORY_USAGE_CPU_ONLY, s_stagingBuffer, s_stagingAlloc, &s_stagingMapped);

    s_vertModule = CreateShaderModule(g_gothic_vert_spv, sizeof(g_gothic_vert_spv));
    s_fragModule = CreateShaderModule(g_gothic_frag_spv, sizeof(g_gothic_frag_spv));

    VkDescriptorSetLayoutBinding samplerBinding = {};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = {};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &samplerBinding;
    vkCreateDescriptorSetLayout(device, &dslci, nullptr, &s_descSetLayout);

    VkPushConstantRange pcRange = {};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstants);

    VkDescriptorSetLayout setLayouts[2] = { s_descSetLayout, s_descSetLayout };

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 2;
    plci.pSetLayouts = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(device, &plci, nullptr, &s_pipelineLayout);

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 4096;

    VkDescriptorPoolCreateInfo dpci = {};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 4096;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(device, &dpci, nullptr, &s_descPool);

    s_defaultSampler = CreateSampler(1);
    CreateWhiteTexture();

    MakeIdentity(s_worldMatrix);
    MakeIdentity(s_viewMatrix);
    MakeIdentity(s_projMatrix);

    s_initialized = true;

    {
        char dllPath[MAX_PATH] = {};
        HMODULE hm = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&Init), &hm);
        GetModuleFileNameA(hm, dllPath, MAX_PATH);
        std::string dir(dllPath);
        auto sep = dir.find_last_of("\\/");
        if (sep != std::string::npos) dir = dir.substr(0, sep + 1);
        std::string gvlkDir = dir + "GVulkan\\";
        CreateDirectoryA(gvlkDir.c_str(), nullptr);
        s_timeCycle.Load(gvlkDir + "timecycle.cfg");
    }

    ImGuiManager::Init();
}

void Shutdown() {
    if (!s_initialized) return;
    VkDevice device = GVulkan_GetDevice();
    if (!device) return;

    vkDeviceWaitIdle(device);

    ImGuiManager::Shutdown();

    if (s_whiteTex.view)    vkDestroyImageView(device, s_whiteTex.view, nullptr);
    if (s_whiteTex.sampler) vkDestroySampler(device, s_whiteTex.sampler, nullptr);
    if (s_whiteTex.image)   vmaDestroyImage(GVulkan_GetAllocator(), s_whiteTex.image, s_whiteTex.alloc);

    s_pipelineCache.clear(device);

    if (s_defaultSampler)   vkDestroySampler(device, s_defaultSampler, nullptr);
    if (s_pipelineLayout)   vkDestroyPipelineLayout(device, s_pipelineLayout, nullptr);
    if (s_descSetLayout)    vkDestroyDescriptorSetLayout(device, s_descSetLayout, nullptr);
    if (s_descPool)         vkDestroyDescriptorPool(device, s_descPool, nullptr);
    if (s_vertModule)       vkDestroyShaderModule(device, s_vertModule, nullptr);
    if (s_fragModule)       vkDestroyShaderModule(device, s_fragModule, nullptr);

    for (int i = 0; i < MAX_FRAMES; i++) {
        if (s_vertexBuffer[i]) vmaDestroyBuffer(GVulkan_GetAllocator(), s_vertexBuffer[i], s_vertexAlloc[i]);
        if (s_indexBuffer[i])  vmaDestroyBuffer(GVulkan_GetAllocator(), s_indexBuffer[i], s_indexAlloc[i]);
    }
    if (s_stagingBuffer) vmaDestroyBuffer(GVulkan_GetAllocator(), s_stagingBuffer, s_stagingAlloc);

    if (s_cmdPool) vkDestroyCommandPool(device, s_cmdPool, nullptr);

    s_initialized = false;
}

void BeginFrame(int windowW, int windowH, int gameW, int gameH) {
    if (s_frameActive) {
        EndFrame();
    }

    s_timeCycle.Update();
    ImGuiManager::PollInput();

    s_windowW = windowW;
    s_windowH = windowH;
    s_gameW = gameW;
    s_gameH = gameH;
    s_in2DMode = true;

    if (GVulkan_NeedsSwapchainRecreate()) {
        GVulkan_RecreateSwapchain();
    }

    VkCommandBuffer cmd = s_cmdBufs[s_frameIndex];
    if (!GVulkan_BeginFrame(cmd))
        return;

    s_frameActive = true;
    s_vertexOffset = 0;
    s_indexOffset = 0;
    s_curVerts  = s_vertexMapped[s_frameIndex];
    s_curIdx    = s_indexMapped[s_frameIndex];
    s_curVertBuf = s_vertexBuffer[s_frameIndex];
    s_curIdxBuf  = s_indexBuffer[s_frameIndex];

    VkExtent2D ext = GVulkan_GetSwapExtent();
    VkViewport vp = {};
    vp.width = (float)ext.width;
    vp.height = (float)ext.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {};
    scissor.extent = ext;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    ImGuiManager::NewFrame();
}

void EndFrame() {
    if (!s_frameActive) return;

    ImGuiManager::Render(s_cmdBufs[s_frameIndex]);

    s_frameActive = false;
    GVulkan_EndFrame(s_cmdBufs[s_frameIndex]);
    s_frameIndex = (s_frameIndex + 1) % MAX_FRAMES;
}

void Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
    if (!s_frameActive) return;

    uint32_t count = 0;
    VkClearAttachment atts[2] = {};

    if (flags & D3DCLEAR_TARGET) {
        atts[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        atts[count].colorAttachment = 0;
        atts[count].clearValue.color.float32[0] = ((color >> 16) & 0xFF) / 255.0f;
        atts[count].clearValue.color.float32[1] = ((color >>  8) & 0xFF) / 255.0f;
        atts[count].clearValue.color.float32[2] = ( color        & 0xFF) / 255.0f;
        atts[count].clearValue.color.float32[3] = ((color >> 24) & 0xFF) / 255.0f;
        count++;
    }
    if (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) {
        atts[count].aspectMask = 0;
        if (flags & D3DCLEAR_ZBUFFER) atts[count].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (flags & D3DCLEAR_STENCIL) atts[count].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        atts[count].clearValue.depthStencil.depth = z;
        atts[count].clearValue.depthStencil.stencil = stencil;
        count++;
    }

    if (count > 0) {
        VkExtent2D ext = GVulkan_GetSwapExtent();
        VkClearRect rect = {};
        rect.rect.extent = ext;
        rect.layerCount = 1;
        vkCmdClearAttachments(s_cmdBufs[s_frameIndex], count, atts, 1, &rect);
    }
}

VkTexHandle* UploadTexture(DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt, int totalMipLevels) {
    if (!data || w == 0 || h == 0) return nullptr;

    VkDevice device = GVulkan_GetDevice();
    auto* tex = new VkTexHandle();
    tex->width = w;
    tex->height = h;

    bool compressed = (fmt.dwFlags & DDPF_FOURCC) != 0;
    VkFormat vkFmt = compressed ? FourCCToVkFormat(fmt.dwFourCC) : VK_FORMAT_R8G8B8A8_UNORM;
    if (vkFmt == VK_FORMAT_UNDEFINED) { delete tex; return nullptr; }
    tex->format = vkFmt;

    if (compressed) {
        DWORD cc = fmt.dwFourCC;
        tex->hasAlpha = (cc == MAKEFOURCC('D','X','T','3') || cc == MAKEFOURCC('D','X','T','5'));
    } else {
        tex->hasAlpha = (fmt.dwFlags & DDPF_ALPHAPIXELS) != 0;
    }

    uint32_t mips = (uint32_t)totalMipLevels;
    if (mips <= 1) mips = 1;
    uint32_t maxPossible = CalcMipLevels(w, h);
    if (mips > maxPossible) mips = maxPossible;
    if (compressed && (w < 4 || h < 4)) mips = 1;
    tex->mipLevels = mips;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mips > 1) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = vkFmt;
    ici.extent = {w, h, 1};
    ici.mipLevels = mips;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(GVulkan_GetAllocator(), &ici, &aci, &tex->image, &tex->alloc, nullptr);

    if (compressed) {
        DWORD blockSize = FourCCBlockSize(fmt.dwFourCC);
        DWORD bw = (w + 3) / 4;
        DWORD bh = (h + 3) / 4;
        DWORD dataSize = bw * bh * blockSize;
        UploadImageData(tex->image, w, h, 0, data, dataSize);
    } else {
        std::vector<unsigned char> rgba;
        ConvertToRGBA(data, rgba, w, h, pitch, fmt);
        UploadImageData(tex->image, w, h, 0, rgba.data(), (DWORD)rgba.size());
    }

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = tex->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = vkFmt;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1 };
    vkCreateImageView(device, &vci, nullptr, &tex->view);

    tex->sampler = CreateSampler(mips);
    tex->descSet = AllocateDescriptorSet();
    UpdateDescriptorSet(tex->descSet, tex->view, tex->sampler);

    return tex;
}

void UpdateTexture(VkTexHandle* tex, DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    if (!tex || !data || w == 0 || h == 0) return;

    bool compressed = (fmt.dwFlags & DDPF_FOURCC) != 0;
    if (compressed) {
        DWORD blockSize = FourCCBlockSize(fmt.dwFourCC);
        DWORD bw = (w + 3) / 4;
        DWORD bh = (h + 3) / 4;
        DWORD dataSize = bw * bh * blockSize;
        UploadImageData(tex->image, w, h, 0, data, dataSize);
    } else {
        std::vector<unsigned char> rgba;
        ConvertToRGBA(data, rgba, w, h, pitch, fmt);
        UploadImageData(tex->image, w, h, 0, rgba.data(), (DWORD)rgba.size());
    }
}

void UploadTextureMipLevel(VkTexHandle* tex, int level, DWORD w, DWORD h,
                           const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    if (!tex || !data || w == 0 || h == 0) return;
    if ((uint32_t)level >= tex->mipLevels) return;

    bool compressed = (fmt.dwFlags & DDPF_FOURCC) != 0;
    if (compressed) {
        DWORD blockSize = FourCCBlockSize(fmt.dwFourCC);
        DWORD bw = (w + 3) / 4;
        DWORD bh = (h + 3) / 4;
        DWORD dataSize = bw * bh * blockSize;
        UploadImageData(tex->image, w, h, (uint32_t)level, data, dataSize);
    } else {
        std::vector<unsigned char> rgba;
        ConvertToRGBA(data, rgba, w, h, pitch, fmt);
        UploadImageData(tex->image, w, h, (uint32_t)level, rgba.data(), (DWORD)rgba.size());
    }
}

void SetTextureMipmapParams(VkTexHandle* tex, int mipCount) {
    if (!tex || mipCount <= 0) return;

    uint32_t providedLevels = (uint32_t)mipCount + 1;
    bool isCompressed = (tex->format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ||
                         tex->format == VK_FORMAT_BC2_UNORM_BLOCK ||
                         tex->format == VK_FORMAT_BC3_UNORM_BLOCK);

    if (!isCompressed && providedLevels < tex->mipLevels) {
        GenerateMipmaps(tex->image, tex->width, tex->height, tex->mipLevels, providedLevels - 1);
    }
}

void FreeTexture(VkTexHandle* tex) {
    if (!tex) return;
    VkDevice device = GVulkan_GetDevice();
    if (!device) { delete tex; return; }
    vkDeviceWaitIdle(device);
    if (tex->descSet && s_descPool) vkFreeDescriptorSets(device, s_descPool, 1, &tex->descSet);
    if (tex->view)    vkDestroyImageView(device, tex->view, nullptr);
    if (tex->sampler) vkDestroySampler(device, tex->sampler, nullptr);
    if (tex->image)   vmaDestroyImage(GVulkan_GetAllocator(), tex->image, tex->alloc);
    delete tex;
}

void BindTexture(VkTexHandle* tex) {
    s_boundTexture = tex;
}

void BindTexture2(VkTexHandle* tex) {
    s_boundTexture2 = tex;
}

void SetAlphaBlendEnabled(bool enabled) { s_blendEnabled = enabled; }
void SetBlendFunc(DWORD src, DWORD dst) { s_srcBlend = src; s_dstBlend = dst; }
void SetAlphaTestEnabled(bool enabled)  { s_alphaTestEnabled = enabled; }
void SetAlphaRef(DWORD ref)             { s_alphaRef = ref; }
void SetDepthEnabled(bool enabled)      { s_depthEnabled = enabled; }
void SetDepthWriteEnabled(bool enabled) { s_depthWriteEnabled = enabled; }
void SetDepthFunc(DWORD func)           { s_depthFunc = func; }

void SetStageColorOp(int stage, DWORD op) {
    if (stage == 0) s_stage0ColorOp = op;
    else if (stage == 1) s_stage1ColorOp = op;
}

void SetStageColorArg(int stage, int argIndex, DWORD value) {
    value &= 0xF;
    if (stage == 0) {
        if (argIndex == 1) s_stage0Arg1 = value;
        else               s_stage0Arg2 = value;
    } else if (stage == 1) {
        if (argIndex == 1) s_stage1Arg1 = value;
        else               s_stage1Arg2 = value;
    }
}

void SetStageAlphaOp(int stage, DWORD op) {
    if (stage == 0) s_stage0AlphaOp = op;
}

void SetStageAlphaArg(int stage, int argIndex, DWORD value) {
    value &= 0xF;
    if (stage == 0) {
        if (argIndex == 1) s_stage0AlphaArg1 = value;
        else               s_stage0AlphaArg2 = value;
    }
}

void SetTextureFactor(DWORD factor) { s_textureFactor = factor; }

void SetTextureAddress2U(DWORD d3dAddr) { s_stage1AddrU = d3dAddr; }
void SetTextureAddress2V(DWORD d3dAddr) { s_stage1AddrV = d3dAddr; }

void SetStageTexCoordIndex(int stage, DWORD value) {
    DWORD uvIdx = (value > 7) ? 0 : value;
    if (stage == 0) s_stage0TexCoordIdx = uvIdx;
    else if (stage == 1) s_stage1TexCoordIdx = uvIdx;
}

void SetWorldMatrix(const float* m)      { memcpy(s_worldMatrix, m, 64); }
void SetViewMatrix(const float* m)       { memcpy(s_viewMatrix, m, 64); }
void SetProjectionMatrix(const float* m) { memcpy(s_projMatrix, m, 64); }

void SetViewport(DWORD x, DWORD y, DWORD w, DWORD h) {
    s_vpX = x; s_vpY = y; s_vpW = w; s_vpH = h;
}

void SetTextureAddressU(DWORD d3dAddr) { s_addressU = d3dAddr; }
void SetTextureAddressV(DWORD d3dAddr) { s_addressV = d3dAddr; }

static int TexCoordFloats(DWORD fvf, int setIndex) {
    static const int sizes[] = {2, 3, 4, 1};
    int bits = (fvf >> (setIndex * 2 + 16)) & 3;
    return sizes[bits];
}

static DWORD CalcFVFStride(DWORD fvf) {
    DWORD stride = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZ:    stride = 12; break;
        case D3DFVF_XYZRHW: stride = 16; break;
        case D3DFVF_XYZB1:  stride = 16; break;
        case D3DFVF_XYZB2:  stride = 20; break;
        case D3DFVF_XYZB3:  stride = 24; break;
        case D3DFVF_XYZB4:  stride = 28; break;
        case D3DFVF_XYZB5:  stride = 32; break;
    }
    if (fvf & D3DFVF_NORMAL)   stride += 12;
    if (fvf & D3DFVF_DIFFUSE)  stride += 4;
    if (fvf & D3DFVF_SPECULAR) stride += 4;
    DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD i = 0; i < texCount; i++)
        stride += TexCoordFloats(fvf, i) * 4;
    return stride;
}

/* ── Gothic FVF defines (matching GD3D11/MyDirect3DDevice7.h) ────────── */
#define GOTHIC_FVF_XYZ_DIF_T1         (D3DFVF_XYZ    | D3DFVF_DIFFUSE  | D3DFVF_TEX1)
#define GOTHIC_FVF_XYZ_NRM_T1         (D3DFVF_XYZ    | D3DFVF_NORMAL   | D3DFVF_TEX1)
#define GOTHIC_FVF_XYZ_DIF_T2         (D3DFVF_XYZ    | D3DFVF_DIFFUSE  | D3DFVF_TEX2)
#define GOTHIC_FVF_XYZ_NRM_DIF_T2     (D3DFVF_XYZ    | D3DFVF_NORMAL   | D3DFVF_DIFFUSE | D3DFVF_TEX2)
#define GOTHIC_FVF_XYZRHW_DIF_T1      (D3DFVF_XYZRHW | D3DFVF_DIFFUSE  | D3DFVF_TEX1)
#define GOTHIC_FVF_XYZRHW_DIF_SPEC_T1 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE  | D3DFVF_SPECULAR | D3DFVF_TEX1)

/** Convert FVF vertex data to GVertex, matching GD3D11's DrawPrimitive conversion.
 *  For XYZRHW vertices, RHW is stored in Normal.x (same as GD3D11 stores in ExVertexStruct.Normal.x).
 *  For typed FVF combos we use the same typed structs as GD3D11 (Gothic_XYZRHW_DIF_T1_Vertex etc.).
 *  Unknown FVF combos fall through to generic field-by-field parsing. */
static GVertex ConvertVertex(const unsigned char* ptr, DWORD fvf) {
    GVertex v = {};

    // ── Fast paths for known Gothic FVF types (matching GD3D11) ──────
    switch (fvf) {
    case GOTHIC_FVF_XYZRHW_DIF_T1: {
        auto* src = reinterpret_cast<const Gothic_XYZRHW_DIF_T1_Vertex*>(ptr);
        v.px = src->x;  v.py = src->y;  v.pz = src->z;
        v.nx = src->rhw; // RHW stored in Normal.x — same as GD3D11
        v.color = src->color;
        v.u = src->u;  v.v = src->v;
        return v;
    }
    case GOTHIC_FVF_XYZRHW_DIF_SPEC_T1: {
        auto* src = reinterpret_cast<const Gothic_XYZRHW_DIF_SPEC_T1_Vertex*>(ptr);
        v.px = src->x;  v.py = src->y;  v.pz = src->z;
        v.nx = src->rhw; // RHW stored in Normal.x — same as GD3D11
        v.color = src->color;
        v.u = src->u;  v.v = src->v;
        return v;
    }
    case GOTHIC_FVF_XYZ_DIF_T1: {
        auto* src = reinterpret_cast<const Gothic_XYZ_DIF_T1_Vertex*>(ptr);
        v.px = src->x;  v.py = src->y;  v.pz = src->z;
        v.color = src->color;
        v.u = src->u;  v.v = src->v;
        return v;
    }
    case GOTHIC_FVF_XYZ_NRM_T1: {
        auto* src = reinterpret_cast<const Gothic_XYZ_NRM_T1_Vertex*>(ptr);
        v.px = src->x;   v.py = src->y;   v.pz = src->z;
        v.nx = src->nx;  v.ny = src->ny;  v.nz = src->nz;
        v.u = src->u;  v.v = src->v;
        return v;
    }
    case GOTHIC_FVF_XYZ_DIF_T2: {
        auto* src = reinterpret_cast<const Gothic_XYZ_DIF_T2_Vertex*>(ptr);
        v.px = src->x;  v.py = src->y;  v.pz = src->z;
        v.color = src->color;
        v.u = src->u;    v.v = src->v;
        v.u2 = src->u2;  v.v2 = src->v2;
        return v;
    }
    case GOTHIC_FVF_XYZ_NRM_DIF_T2: {
        auto* src = reinterpret_cast<const Gothic_XYZ_NRM_DIF_T2_Vertex*>(ptr);
        v.px = src->x;   v.py = src->y;   v.pz = src->z;
        v.nx = src->nx;  v.ny = src->ny;  v.nz = src->nz;
        v.color = src->color;
        v.u = src->u;    v.v = src->v;
        v.u2 = src->u2;  v.v2 = src->v2;
        return v;
    }
    default:
        break;
    }

    // ── Generic FVF parsing for unknown combos ───────────────────────
    DWORD off = 0;

    v.px = *(float*)(ptr + off); off += 4;
    v.py = *(float*)(ptr + off); off += 4;
    v.pz = *(float*)(ptr + off); off += 4;

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    if (isRHW) {
        v.nx = *(float*)(ptr + off); // Store RHW in Normal.x (matching GD3D11)
        off += 4;
    }

    int extraFloats = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZB1: extraFloats = 1; break;
        case D3DFVF_XYZB2: extraFloats = 2; break;
        case D3DFVF_XYZB3: extraFloats = 3; break;
        case D3DFVF_XYZB4: extraFloats = 4; break;
        case D3DFVF_XYZB5: extraFloats = 5; break;
    }
    off += extraFloats * 4;

    if (fvf & D3DFVF_NORMAL) {
        v.nx = *(float*)(ptr + off); off += 4;
        v.ny = *(float*)(ptr + off); off += 4;
        v.nz = *(float*)(ptr + off); off += 4;
    }

    if (fvf & D3DFVF_DIFFUSE) {
        v.color = *(DWORD*)(ptr + off); off += 4;
    }

    if (fvf & D3DFVF_SPECULAR) off += 4;

    DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (texCount >= 1) {
        int nf = TexCoordFloats(fvf, 0);
        v.u = *(float*)(ptr + off);
        v.v = (nf >= 2) ? *(float*)(ptr + off + 4) : 0.0f;
        off += nf * 4;
    }
    if (texCount >= 2) {
        int nf = TexCoordFloats(fvf, 1);
        v.u2 = *(float*)(ptr + off);
        v.v2 = (nf >= 2) ? *(float*)(ptr + off + 4) : 0.0f;
    }

    return v;
}

static VkPrimitiveTopology D3DPrimToVk(D3DPRIMITIVETYPE type) {
    switch (type) {
    case D3DPT_POINTLIST:    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case D3DPT_LINELIST:     return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case D3DPT_LINESTRIP:    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case D3DPT_TRIANGLELIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case D3DPT_TRIANGLEFAN:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static void ConvertFanToList(const unsigned char* srcVerts, DWORD fvf, DWORD stride,
                             DWORD count, uint32_t& outVertStart, uint32_t& outVertCount) {
    if (count < 3 || s_vertexOffset + (count - 2) * 3 > MAX_VERTICES) {
        outVertCount = 0;
        return;
    }

    GVertex v0 = ConvertVertex(srcVerts, fvf);
    outVertStart = s_vertexOffset;
    outVertCount = 0;

    for (DWORD i = 1; i + 1 < count; i++) {
        GVertex v1 = ConvertVertex(srcVerts + i * stride, fvf);
        GVertex v2 = ConvertVertex(srcVerts + (i + 1) * stride, fvf);
        s_curVerts[s_vertexOffset++] = v0;
        s_curVerts[s_vertexOffset++] = v1;
        s_curVerts[s_vertexOffset++] = v2;
        outVertCount += 3;
    }
}

static void ComputeMVP(float* mvp, bool isRHW) {
    if (isRHW) {
        // XYZRHW: the vertex shader does TransformXYZRHW (matching GD3D11's
        // VS_TransformedEx.hlsl), so we pass identity as the MVP.
        MakeIdentity(mvp);
    } else {
        float wvp[16], tmp[16];

        MatMul(tmp, s_worldMatrix, s_viewMatrix);
        MatMul(wvp, tmp, s_projMatrix);

        // D3D row-major WVP passed to GLSL column-major is an implicit transpose,
        // which is exactly the conversion from row-vector (v*M) to column-vector (M^T*v).
        // No explicit Transpose needed -- just negate column 1 of the row-major matrix
        // to flip Vulkan's inverted Y clip space.
        wvp[1]  = -wvp[1];
        wvp[5]  = -wvp[5];
        wvp[9]  = -wvp[9];
        wvp[13] = -wvp[13];

        memcpy(mvp, wvp, 64);
    }
}

// Convert triangle strip to triangle list (like ConvertFanToList but for strips)
static void ConvertStripToList(const unsigned char* srcVerts, DWORD fvf, DWORD stride,
                               DWORD count, uint32_t& outVertStart, uint32_t& outVertCount) {
    if (count < 3 || s_vertexOffset + (count - 2) * 3 > MAX_VERTICES) {
        outVertCount = 0;
        return;
    }

    outVertStart = s_vertexOffset;
    outVertCount = 0;

    for (DWORD i = 0; i + 2 < count; i++) {
        GVertex v0 = ConvertVertex(srcVerts + i * stride, fvf);
        GVertex v1 = ConvertVertex(srcVerts + (i + 1) * stride, fvf);
        GVertex v2 = ConvertVertex(srcVerts + (i + 2) * stride, fvf);
        // Alternate winding for even/odd triangles (strip convention)
        if (i & 1) {
            s_curVerts[s_vertexOffset++] = v0;
            s_curVerts[s_vertexOffset++] = v2;
            s_curVerts[s_vertexOffset++] = v1;
        } else {
            s_curVerts[s_vertexOffset++] = v0;
            s_curVerts[s_vertexOffset++] = v1;
            s_curVerts[s_vertexOffset++] = v2;
        }
        outVertCount += 3;
    }
}

static void EmitDrawCall(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
                         DWORD count, const WORD* indices, DWORD indexCount) {
    if (!s_frameActive || count == 0) return;

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    DWORD stride = CalcFVFStride(fvf);
    const unsigned char* src = (const unsigned char*)vertices;

    bool isFan = (type == D3DPT_TRIANGLEFAN);
    bool isStrip = (type == D3DPT_TRIANGLESTRIP);

    uint32_t vertStart, vertCount;
    uint32_t idxStart = 0, idxCount2 = 0;

    if (indices && indexCount > 0 && !isFan && !isStrip) {
        vertStart = s_vertexOffset;
        vertCount = count;
        if (s_vertexOffset + count > MAX_VERTICES || s_indexOffset + indexCount > MAX_INDICES) return;
        for (DWORD i = 0; i < count; i++)
            s_curVerts[s_vertexOffset + i] = ConvertVertex(src + i * stride, fvf);
        s_vertexOffset += count;

        idxStart = s_indexOffset;
        idxCount2 = indexCount;
        memcpy(s_curIdx + s_indexOffset, indices, indexCount * sizeof(uint16_t));
        s_indexOffset += indexCount;
    } else if (isFan) {
        ConvertFanToList(src, fvf, stride, count, vertStart, vertCount);
        if (vertCount == 0) return;
    } else if (isStrip) {
        ConvertStripToList(src, fvf, stride, count, vertStart, vertCount);
        if (vertCount == 0) return;
    } else {
        vertStart = s_vertexOffset;
        vertCount = count;
        if (s_vertexOffset + count > MAX_VERTICES) return;
        for (DWORD i = 0; i < count; i++)
            s_curVerts[s_vertexOffset + i] = ConvertVertex(src + i * stride, fvf);
        s_vertexOffset += count;
    }

    PushConstants pc = {};
    ComputeMVP(pc.mvp, isRHW);
    pc.flags = 0;
    if (s_boundTexture) pc.flags |= 1;
    if (s_alphaTestEnabled) pc.flags |= 2;
    if (s_boundTexture2 && s_stage1ColorOp > 1) pc.flags |= 4;
    if (s_boundTexture && s_boundTexture->hasAlpha) pc.flags |= 8;
    if (isRHW) pc.flags |= 32; // bit 5 = isRHW — shader uses TransformXYZRHW
    pc.alphaRef = s_alphaRef / 255.0f;
    pc.stage0ColorOp = s_stage0ColorOp;
    pc.stage1ColorOp = s_stage1ColorOp;
    pc.stage0Args = (s_stage0Arg2 << 16) | s_stage0Arg1;
    pc.stage1Args = (s_stage1Arg2 << 16) | s_stage1Arg1;
    pc.texCoordIdx = (s_stage1TexCoordIdx << 16) | s_stage0TexCoordIdx;
    pc.stage0AlphaOp = s_stage0AlphaOp;
    pc.stage0AlphaArgs = (s_stage0AlphaArg2 << 16) | s_stage0AlphaArg1;
    pc.textureFactor = s_textureFactor;
    if (!isRHW && s_timeCycle.IsEnabled()) {
        pc.flags |= 16u;
        pc.timecycleColor = s_timeCycle.GetPackedColor();
    }
    // Viewport info for TransformXYZRHW (matching GD3D11's BindViewportInformation
    // which reads the D3D11 viewport = full rendering resolution, not the D3D7 sub-viewport)
    if (isRHW) {
        pc.vpPos[0]  = 0.0f;
        pc.vpPos[1]  = 0.0f;
        pc.vpSize[0] = (float)s_gameW;
        pc.vpSize[1] = (float)s_gameH;
    } else {
        pc.vpPos[0]  = (float)s_vpX;
        pc.vpPos[1]  = (float)s_vpY;
        pc.vpSize[0] = (float)s_vpW;
        pc.vpSize[1] = (float)s_vpH;
    }

    PipelineKey key = {};
    key.blendEnabled = s_blendEnabled ? 1 : 0;
    // Enable depth test for all draws when app requests it (including RHW/2D particles so they respect scene depth)
    key.depthTestEnabled = s_depthEnabled ? 1 : 0;
    key.depthWriteEnabled = (!isRHW && s_depthWriteEnabled) ? 1 : 0;
    key.depthFunc = (uint8_t)s_depthFunc;
    key.srcBlend = (uint8_t)s_srcBlend;
    key.dstBlend = (uint8_t)s_dstBlend;

    VkPipeline pipeline = GetOrCreatePipeline(key);
    if (pipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(s_cmdBufs[s_frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(s_cmdBufs[s_frameIndex], s_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    VkDescriptorSet descSets[2];
    descSets[0] = s_boundTexture ? s_boundTexture->descSet : s_whiteTex.descSet;
    descSets[1] = s_boundTexture2 ? s_boundTexture2->descSet : s_whiteTex.descSet;
    if (descSets[0] && descSets[1]) {
        vkCmdBindDescriptorSets(s_cmdBufs[s_frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                s_pipelineLayout, 0, 2, descSets, 0, nullptr);
    }

    VkExtent2D ext = GVulkan_GetSwapExtent();
    VkViewport vp = {};
    VkRect2D scissor = {};

    if (isRHW) {
        // TransformXYZRHW in the shader converts viewport coords → NDC,
        // so use the full swapchain extent (matching GD3D11's DrawVertexArray)
        vp.x = 0; vp.y = 0;
        vp.width  = (float)ext.width;
        vp.height = (float)ext.height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        scissor.extent = ext;
    } else {
        // Scale the D3D game viewport to the Vulkan swapchain
        float sx = (float)ext.width  / (float)s_gameW;
        float sy = (float)ext.height / (float)s_gameH;
        vp.x = s_vpX * sx;
        vp.y = s_vpY * sy;
        vp.width  = s_vpW * sx;
        vp.height = s_vpH * sy;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        scissor.offset = { (int32_t)vp.x, (int32_t)vp.y };
        scissor.extent = { (uint32_t)vp.width, (uint32_t)vp.height };
    }
    vkCmdSetViewport(s_cmdBufs[s_frameIndex], 0, 1, &vp);
    vkCmdSetScissor(s_cmdBufs[s_frameIndex], 0, 1, &scissor);

    VkDeviceSize vbOffset = vertStart * sizeof(GVertex);
    vkCmdBindVertexBuffers(s_cmdBufs[s_frameIndex], 0, 1, &s_curVertBuf, &vbOffset);

    if (idxCount2 > 0) {
        VkDeviceSize ibOffset = idxStart * sizeof(uint16_t);
        vkCmdBindIndexBuffer(s_cmdBufs[s_frameIndex], s_curIdxBuf, ibOffset, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(s_cmdBufs[s_frameIndex], idxCount2, 1, 0, 0, 0);
    } else {
        vkCmdDraw(s_cmdBufs[s_frameIndex], vertCount, 1, 0, 0);
    }
}

void DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices, DWORD count) {
    EmitDrawCall(type, fvf, vertices, count, nullptr, 0);
}

void DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
                          DWORD vertexCount, const WORD* indices, DWORD indexCount) {
    EmitDrawCall(type, fvf, vertices, vertexCount, indices, indexCount);
}

// ---------------------------------------------------------------------------
// Build a left-handed perspective projection matrix (row-major, D3D convention)
// Depth range [0, 1], matching D3D/Vulkan.
// ---------------------------------------------------------------------------
static void MakePerspectiveLH(float* out, float fovYRad, float aspect, float zn, float zf) {
    memset(out, 0, 16 * sizeof(float));
    float yscale = 1.0f / tanf(fovYRad * 0.5f);
    float xscale = yscale / aspect;
    out[0]  = xscale;
    out[5]  = yscale;
    out[10] = zf / (zf - zn);
    out[11] = 1.0f;
    out[14] = -zn * zf / (zf - zn);
}

void DrawReconstructedWorld() {
    if (!s_frameActive) return;

    const auto& worldVerts = WorldReconstructor::GetWorldVertices();
    const auto& batches    = WorldReconstructor::GetBatches();
    if (worldVerts.empty() || batches.empty()) return;

    uint32_t totalVerts = (uint32_t)worldVerts.size();
    if (s_vertexOffset + totalVerts > MAX_VERTICES) {
        totalVerts = MAX_VERTICES - s_vertexOffset;
        totalVerts -= totalVerts % 3;
        if (totalVerts == 0) return;
    }

    // Upload all world vertices as GVertex (with UVs + UV2)
    uint32_t uploadStart = s_vertexOffset;
    for (uint32_t i = 0; i < totalVerts; i++) {
        GVertex gv = {};
        gv.px = worldVerts[i].x;
        gv.py = worldVerts[i].y;
        gv.pz = worldVerts[i].z;
        gv.u  = worldVerts[i].u;
        gv.v  = worldVerts[i].v;
        gv.u2 = worldVerts[i].u2;
        gv.v2 = worldVerts[i].v2;
        gv.color = worldVerts[i].color;
        s_curVerts[s_vertexOffset++] = gv;
    }

    // Build VP = View * Gothic's projection (matches Gothic's BSP/portal culling frustum)
    float vp[16];
    MatMul(vp, s_viewMatrix, s_projMatrix);

    // Flip Y for Vulkan NDC
    vp[1]  = -vp[1];
    vp[5]  = -vp[5];
    vp[9]  = -vp[9];
    vp[13] = -vp[13];

    VkCommandBuffer cmd = s_cmdBufs[s_frameIndex];

    // Bind vertex buffer (all vertices are contiguous)
    VkDeviceSize vbOffset = uploadStart * sizeof(GVertex);
    vkCmdBindVertexBuffers(cmd, 0, 1, &s_curVertBuf, &vbOffset);

    VkPipeline lastPipeline = VK_NULL_HANDLE;

    // Compute viewport scaling (matching EmitDrawCall for 3D draws)
    VkExtent2D ext = GVulkan_GetSwapExtent();
    float sx = (float)ext.width  / (float)s_gameW;
    float sy = (float)ext.height / (float)s_gameH;

    // Draw each batch with its own texture and full render state
    for (const auto& batch : batches) {
        // Skip batches that exceed what we uploaded
        if (batch.startVertex + batch.vertexCount > totalVerts) break;

        const auto& rs = batch.rs;

        // Per-batch pipeline based on blend/alpha/depth state
        PipelineKey key = {};
        key.depthTestEnabled = 1;
        key.depthWriteEnabled = rs.depthWriteEnabled ? 1 : 0;
        key.depthFunc = (uint8_t)D3DCMP_LESSEQUAL;
        key.blendEnabled = rs.blendEnabled ? 1 : 0;
        key.srcBlend = rs.srcBlend;
        key.dstBlend = rs.dstBlend;

        VkPipeline pipeline = GetOrCreatePipeline(key);
        if (pipeline == VK_NULL_HANDLE) continue;
        if (pipeline != lastPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            lastPipeline = pipeline;
        }

        // Build push constants matching EmitDrawCall's logic for 3D draws
        PushConstants pc = {};
        memcpy(pc.mvp, vp, 64);

        pc.flags = 0;
        if (batch.texture)   pc.flags |= 1;  // bit0 = hasTex0
        if (rs.alphaTestEnabled) pc.flags |= 2;  // bit1 = alphaTest
        if (rs.texture2 && rs.stage1ColorOp > 1) pc.flags |= 4;  // bit2 = hasTex1
        if (batch.texture && batch.texture->hasAlpha) pc.flags |= 8;  // bit3 = texHasAlpha
        if (s_timeCycle.IsEnabled()) {
            pc.flags |= 16u; // bit4 = timecycle
            pc.timecycleColor = s_timeCycle.GetPackedColor();
        }

        pc.alphaRef       = rs.alphaRef;
        pc.stage0ColorOp  = rs.stage0ColorOp;
        pc.stage1ColorOp  = rs.stage1ColorOp;
        pc.stage0Args     = (rs.stage0Arg2 << 16) | rs.stage0Arg1;
        pc.stage1Args     = (rs.stage1Arg2 << 16) | rs.stage1Arg1;
        pc.texCoordIdx    = (rs.stage1TexCoordIdx << 16) | rs.stage0TexCoordIdx;
        pc.stage0AlphaOp  = rs.stage0AlphaOp;
        pc.stage0AlphaArgs = (rs.stage0AlphaArg2 << 16) | rs.stage0AlphaArg1;
        pc.textureFactor  = rs.textureFactor;

        // Viewport info (matching EmitDrawCall's 3D path)
        pc.vpPos[0]  = (float)s_vpX;
        pc.vpPos[1]  = (float)s_vpY;
        pc.vpSize[0] = (float)s_vpW;
        pc.vpSize[1] = (float)s_vpH;

        vkCmdPushConstants(cmd, s_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);

        // Bind textures (stage 0 + stage 1)
        VkDescriptorSet descSets[2];
        descSets[0] = (batch.texture && batch.texture->descSet)
                      ? batch.texture->descSet : s_whiteTex.descSet;
        descSets[1] = (rs.texture2 && rs.texture2->descSet)
                      ? rs.texture2->descSet : s_whiteTex.descSet;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                s_pipelineLayout, 0, 2, descSets, 0, nullptr);

        // Set viewport scaled to swapchain (matching EmitDrawCall's 3D path)
        VkViewport viewport = {};
        viewport.x = s_vpX * sx;
        viewport.y = s_vpY * sy;
        viewport.width  = s_vpW * sx;
        viewport.height = s_vpH * sy;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = { (int32_t)viewport.x, (int32_t)viewport.y };
        scissor.extent = { (uint32_t)viewport.width, (uint32_t)viewport.height };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdDraw(cmd, batch.vertexCount, 1, batch.startVertex, 0);
    }
}

// ---------------------------------------------------------------------------
// State getters for WorldReconstructor snapshots
// ---------------------------------------------------------------------------
DWORD GetStage0ColorOp()     { return s_stage0ColorOp; }
DWORD GetStage1ColorOp()     { return s_stage1ColorOp; }
DWORD GetStage0Arg1()        { return s_stage0Arg1; }
DWORD GetStage0Arg2()        { return s_stage0Arg2; }
DWORD GetStage1Arg1()        { return s_stage1Arg1; }
DWORD GetStage1Arg2()        { return s_stage1Arg2; }
DWORD GetStage0AlphaOp()     { return s_stage0AlphaOp; }
DWORD GetStage0AlphaArg1()   { return s_stage0AlphaArg1; }
DWORD GetStage0AlphaArg2()   { return s_stage0AlphaArg2; }
DWORD GetStage0TexCoordIdx() { return s_stage0TexCoordIdx; }
DWORD GetStage1TexCoordIdx() { return s_stage1TexCoordIdx; }
DWORD GetTextureFactor()     { return s_textureFactor; }
VkTexHandle* GetBoundTexture2() { return s_boundTexture2; }
bool IsTimecycleEnabled()    { return s_timeCycle.IsEnabled(); }
uint32_t GetTimecycleColor() { return s_timeCycle.GetPackedColor(); }

} // namespace VkRenderer
