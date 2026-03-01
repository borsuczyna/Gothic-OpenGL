#include "vk_renderer.h"
#include "../debug.h"
#include "../shaders/gothic_shaders.h"
#include <vector>
#include <cstring>
#include <unordered_map>

struct GVertex {
    float    x, y, z;
    uint32_t color;
    float    u, v;
};

struct PushConstants {
    float    mvp[16];
    uint32_t flags;
    float    alphaRef;
};

struct PipelineKey {
    uint8_t blendEnabled;
    uint8_t depthTestEnabled;
    uint8_t depthWriteEnabled;
    uint8_t depthFunc;
    uint8_t srcBlend;
    uint8_t dstBlend;
    uint8_t pad0, pad1;

    bool operator==(const PipelineKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};

struct PipelineKeyHash {
    size_t operator()(const PipelineKey& k) const {
        uint64_t v;
        memcpy(&v, &k, sizeof(v));
        return std::hash<uint64_t>()(v);
    }
};

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

static std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> s_pipelineCache;

static VkShaderModule s_vertModule = VK_NULL_HANDLE;
static VkShaderModule s_fragModule = VK_NULL_HANDLE;

static VkTexHandle    s_whiteTex;
static VkSampler      s_defaultSampler = VK_NULL_HANDLE;

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
    auto it = s_pipelineCache.find(key);
    if (it != s_pipelineCache.end()) return it->second;

    VkDevice device = GVulkan_GetDevice();

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = s_vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = s_fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(GVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = offsetof(GVertex, x);
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32_UINT;         attrs[1].offset = offsetof(GVertex, color);
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT;    attrs[2].offset = offsetof(GVertex, u);

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAsm = {};
    inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpState = {};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa = {};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = key.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = key.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = D3DCmpToVk(key.depthFunc);

    VkPipelineColorBlendAttachmentState blendAtt = {};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAtt.blendEnable = key.blendEnabled ? VK_TRUE : VK_FALSE;
    blendAtt.srcColorBlendFactor = D3DBlendToVk(key.srcBlend);
    blendAtt.dstColorBlendFactor = D3DBlendToVk(key.dstBlend);
    blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = D3DBlendToVk(key.srcBlend);
    blendAtt.dstAlphaBlendFactor = D3DBlendToVk(key.dstBlend);
    blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blendState = {};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blendAtt;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState = {};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &vpState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &msaa;
    pci.pDepthStencilState = &depthStencil;
    pci.pColorBlendState = &blendState;
    pci.pDynamicState = &dynState;
    pci.layout = s_pipelineLayout;
    pci.renderPass = GVulkan_GetRenderPass();
    pci.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        DbgPrint("ERROR: vkCreateGraphicsPipelines failed (%d)", result);
        return VK_NULL_HANDLE;
    }

    s_pipelineCache[key] = pipeline;
    return pipeline;
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

static void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                                  VkImageLayout newLayout, uint32_t mipLevels) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
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

static void UploadImageData(VkImage image, DWORD w, DWORD h, int mipLevel,
                            const void* data, DWORD dataSize) {
    if (dataSize > STAGING_BUFFER_SIZE) return;
    memcpy(s_stagingMapped, data, dataSize);

    VkCommandBuffer cmd = BeginSingleTimeCommands();
    TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { w, h, 1 };
    vkCmdCopyBufferToImage(cmd, s_stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    EndSingleTimeCommands(cmd);
}

static void TransitionFullImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    TransitionImageLayout(cmd, image, oldLayout, newLayout, mipLevels);
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
    sci.maxAnisotropy = 1.0f;
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

static int s_debugFrameCount = 0;

void Init() {
    if (s_initialized) return;
    printf("[GVulkan] VkRenderer::Init() starting\n"); fflush(stdout);
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

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &s_descSetLayout;
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
    printf("[GVulkan] VkRenderer::Init() complete\n"); fflush(stdout);
}

void Shutdown() {
    if (!s_initialized) return;
    VkDevice device = GVulkan_GetDevice();
    if (!device) return;

    vkDeviceWaitIdle(device);

    if (s_whiteTex.view)    vkDestroyImageView(device, s_whiteTex.view, nullptr);
    if (s_whiteTex.sampler) vkDestroySampler(device, s_whiteTex.sampler, nullptr);
    if (s_whiteTex.image)   vmaDestroyImage(GVulkan_GetAllocator(), s_whiteTex.image, s_whiteTex.alloc);

    for (auto& [key, pipe] : s_pipelineCache) vkDestroyPipeline(device, pipe, nullptr);
    s_pipelineCache.clear();

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

    s_windowW = windowW;
    s_windowH = windowH;
    s_gameW = gameW;
    s_gameH = gameH;
    s_in2DMode = true;

    if (GVulkan_NeedsSwapchainRecreate()) {
        GVulkan_RecreateSwapchain();
    }

    VkCommandBuffer cmd = s_cmdBufs[s_frameIndex];
    if (!GVulkan_BeginFrame(cmd)) {
        if (s_debugFrameCount < 5)
            printf("[GVulkan] BeginFrame FAILED (frame %d)\n", s_debugFrameCount);
        fflush(stdout);
        return;
    }

    s_frameActive = true;
    if (s_debugFrameCount < 5)
        printf("[GVulkan] BeginFrame OK (frame %d, idx=%d)\n", s_debugFrameCount, s_frameIndex);
    fflush(stdout);
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
}

void EndFrame() {
    if (!s_frameActive) return;
    s_frameActive = false;
    if (s_debugFrameCount < 5)
        printf("[GVulkan] EndFrame (frame %d, idx=%d)\n", s_debugFrameCount, s_frameIndex);
    fflush(stdout);
    GVulkan_EndFrame(s_cmdBufs[s_frameIndex]);
    s_frameIndex = (s_frameIndex + 1) % MAX_FRAMES;
    s_debugFrameCount++;
}

void Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
    if (!s_frameActive) {
        if (s_debugFrameCount < 5) { printf("[GVulkan] Clear SKIPPED (frame not active)\n"); fflush(stdout); }
        return;
    }
    if (s_debugFrameCount < 5) { printf("[GVulkan] Clear flags=%lu color=0x%08X\n", flags, color); fflush(stdout); }

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

VkTexHandle* UploadTexture(DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    if (!data || w == 0 || h == 0) return nullptr;

    VkDevice device = GVulkan_GetDevice();
    auto* tex = new VkTexHandle();
    tex->width = w;
    tex->height = h;
    tex->mipLevels = 1;

    bool compressed = (fmt.dwFlags & DDPF_FOURCC) != 0;
    VkFormat vkFmt = compressed ? FourCCToVkFormat(fmt.dwFourCC) : VK_FORMAT_R8G8B8A8_UNORM;
    if (vkFmt == VK_FORMAT_UNDEFINED) { delete tex; return nullptr; }

    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = vkFmt;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(device, &vci, nullptr, &tex->view);

    tex->sampler = CreateSampler(1);
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
        TransitionFullImage(tex->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, tex->mipLevels);
        UploadImageData(tex->image, w, h, 0, data, dataSize);
    } else {
        std::vector<unsigned char> rgba;
        ConvertToRGBA(data, rgba, w, h, pitch, fmt);
        TransitionFullImage(tex->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, tex->mipLevels);
        UploadImageData(tex->image, w, h, 0, rgba.data(), (DWORD)rgba.size());
    }
}

void UploadTextureMipLevel(VkTexHandle* tex, int level, DWORD w, DWORD h,
                           const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    if (!tex || !data || w == 0 || h == 0) return;
    // For mip levels, we need to recreate the image with more mip levels
    // For now, the image was created with 1 mip level, so additional mips are ignored
    // This is handled in SetTextureMipmapParams where we recreate
    (void)level; (void)w; (void)h; (void)data; (void)pitch; (void)fmt;
}

void SetTextureMipmapParams(VkTexHandle* tex, int mipCount) {
    if (!tex || mipCount <= 0) return;
    // Mipmap handling requires recreating the image with proper mip levels.
    // For initial implementation, we accept linear filtering on base level only.
    (void)mipCount;
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

void SetAlphaBlendEnabled(bool enabled) { s_blendEnabled = enabled; }
void SetBlendFunc(DWORD src, DWORD dst) { s_srcBlend = src; s_dstBlend = dst; }
void SetAlphaTestEnabled(bool enabled)  { s_alphaTestEnabled = enabled; }
void SetAlphaRef(DWORD ref)             { s_alphaRef = ref; }
void SetDepthEnabled(bool enabled)      { s_depthEnabled = enabled; }
void SetDepthWriteEnabled(bool enabled) { s_depthWriteEnabled = enabled; }
void SetDepthFunc(DWORD func)           { s_depthFunc = func; }

void SetWorldMatrix(const float* m)      { memcpy(s_worldMatrix, m, 64); }
void SetViewMatrix(const float* m)       { memcpy(s_viewMatrix, m, 64); }
void SetProjectionMatrix(const float* m) { memcpy(s_projMatrix, m, 64); }

void SetViewport(DWORD x, DWORD y, DWORD w, DWORD h) {
    s_vpX = x; s_vpY = y; s_vpW = w; s_vpH = h;
}

void SetTextureAddressU(DWORD d3dAddr) { s_addressU = d3dAddr; }
void SetTextureAddressV(DWORD d3dAddr) { s_addressV = d3dAddr; }

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
    stride += ((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT) * 8;
    return stride;
}

static GVertex ConvertVertex(const unsigned char* ptr, DWORD fvf) {
    GVertex v = {};
    DWORD off = 0;

    v.x = *(float*)(ptr + off); off += 4;
    v.y = *(float*)(ptr + off); off += 4;
    v.z = *(float*)(ptr + off); off += 4;

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    if (isRHW) off += 4;

    int extraFloats = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZB1: extraFloats = 1; break;
        case D3DFVF_XYZB2: extraFloats = 2; break;
        case D3DFVF_XYZB3: extraFloats = 3; break;
        case D3DFVF_XYZB4: extraFloats = 4; break;
        case D3DFVF_XYZB5: extraFloats = 5; break;
    }
    off += extraFloats * 4;

    if (fvf & D3DFVF_NORMAL) off += 12;

    if (fvf & D3DFVF_DIFFUSE) {
        v.color = *(DWORD*)(ptr + off); off += 4;
    } else {
        v.color = 0xFFFFFFFF;
    }

    if (fvf & D3DFVF_SPECULAR) off += 4;

    DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (texCount >= 1) {
        v.u = *(float*)(ptr + off); off += 4;
        v.v = *(float*)(ptr + off); off += 4;
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
        // Map game screen coords directly to Vulkan NDC (column-major)
        // x: [0, gameW] -> [-1, 1], y: [0, gameH] -> [-1, 1]
        // Vulkan NDC y=-1 is top, which matches D3D screen coords y=0 at top
        float gw = (float)s_gameW;
        float gh = (float)s_gameH;

        memset(mvp, 0, 64);
        mvp[0]  =  2.0f / gw;  // col0.x
        mvp[5]  =  2.0f / gh;  // col1.y
        mvp[10] =  1.0f;       // col2.z
        mvp[12] = -1.0f;       // col3.x
        mvp[13] = -1.0f;       // col3.y
        mvp[15] =  1.0f;       // col3.w
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

static int s_drawCount = 0;

static void EmitDrawCall(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
                         DWORD count, const WORD* indices, DWORD indexCount) {
    if (!s_frameActive || count == 0) return;
    if (s_drawCount < 3) {
        printf("[GVulkan] Draw: type=%d fvf=0x%lX verts=%lu idx=%lu\n", type, fvf, count, indexCount);
        fflush(stdout);
        s_drawCount++;
    }

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    DWORD stride = CalcFVFStride(fvf);
    const unsigned char* src = (const unsigned char*)vertices;

    bool isFan = (type == D3DPT_TRIANGLEFAN);

    uint32_t vertStart, vertCount;
    uint32_t idxStart = 0, idxCount2 = 0;

    if (indices && indexCount > 0 && !isFan) {
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
    pc.alphaRef = s_alphaRef / 255.0f;

    PipelineKey key = {};
    key.blendEnabled = s_blendEnabled ? 1 : 0;
    key.depthTestEnabled = (!isRHW && s_depthEnabled) ? 1 : 0;
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

    VkDescriptorSet descSet = s_boundTexture ? s_boundTexture->descSet : s_whiteTex.descSet;
    if (descSet) {
        vkCmdBindDescriptorSets(s_cmdBufs[s_frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                s_pipelineLayout, 0, 1, &descSet, 0, nullptr);
    }

    VkExtent2D ext = GVulkan_GetSwapExtent();
    VkViewport vp = {};
    VkRect2D scissor = {};

    if (isRHW) {
        vp.x = 0; vp.y = 0;
        vp.width = (float)ext.width;
        vp.height = (float)ext.height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        scissor.extent = ext;
    } else {
        float sx = (float)ext.width / (float)s_gameW;
        float sy = (float)ext.height / (float)s_gameH;
        vp.x = s_vpX * sx;
        vp.y = s_vpY * sy;
        vp.width = s_vpW * sx;
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

} // namespace VkRenderer
