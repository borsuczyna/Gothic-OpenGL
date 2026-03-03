#include "ShadowMap.h"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace gvlk {

static const VkFormat SHADOW_DEPTH_FORMAT = VK_FORMAT_D16_UNORM;

bool ShadowMap::Init(VkDevice device, VmaAllocator allocator,
                     VkPipelineLayout pipelineLayout,
                     VkShaderModule vertModule, VkShaderModule shadowFragModule,
                     const ShadowConfig& config) {
    config_ = config;
    uint32_t res = config_.resolution;

    // --- Render pass (depth-only) ---
    VkAttachmentDescription depthAtt = {};
    depthAtt.format         = SHADOW_DEPTH_FORMAT;
    depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 0;
    subpass.pColorAttachments       = nullptr;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2] = {};
    deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass      = 0;
    deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[1].srcSubpass      = 0;
    deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask    = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpci = {};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &depthAtt;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies   = deps;

    if (vkCreateRenderPass(device, &rpci, nullptr, &renderPass_) != VK_SUCCESS) {
        printf("[ShadowMap] Failed to create render pass\n");
        return false;
    }

    // --- Per-cascade depth images, views, framebuffers ---
    for (int i = 0; i < config_.cascadeCount; i++) {
        VkImageCreateInfo ici = {};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = SHADOW_DEPTH_FORMAT;
        ici.extent        = { res, res, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(allocator, &ici, &aci, &images_[i], &allocs_[i], nullptr) != VK_SUCCESS) {
            printf("[ShadowMap] Failed to create depth image %d\n", i);
            return false;
        }

        VkImageViewCreateInfo vci = {};
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = images_[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = SHADOW_DEPTH_FORMAT;
        vci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        vkCreateImageView(device, &vci, nullptr, &depthViews_[i]);

        vci.components.r = VK_COMPONENT_SWIZZLE_R;
        vci.components.g = VK_COMPONENT_SWIZZLE_R;
        vci.components.b = VK_COMPONENT_SWIZZLE_R;
        vci.components.a = VK_COMPONENT_SWIZZLE_ONE;
        vkCreateImageView(device, &vci, nullptr, &sampleViews_[i]);

        VkFramebufferCreateInfo fbci = {};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = renderPass_;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &depthViews_[i];
        fbci.width           = res;
        fbci.height          = res;
        fbci.layers          = 1;
        vkCreateFramebuffer(device, &fbci, nullptr, &framebuffers_[i]);
    }

    // --- Sampler for main-pass sampling ---
    VkSamplerCreateInfo sci = {};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.maxLod       = 1.0f;
    vkCreateSampler(device, &sci, nullptr, &sampler_);

    // --- Shadow pipeline (depth-only, front-face cull, depth bias) ---
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shadowFragModule;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding   = 0;
    binding.stride    = sizeof(GVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[5] = {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(GVertex, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R32_UINT,         (uint32_t)offsetof(GVertex, color) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    (uint32_t)offsetof(GVertex, u) };
    attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,    (uint32_t)offsetof(GVertex, u2) };
    attrs[4] = { 4, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(GVertex, wx) };

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 5;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAsm = {};
    inputAsm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpState = {};
    vpState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode            = VK_POLYGON_MODE_FILL;
    raster.cullMode               = VK_CULL_MODE_FRONT_BIT;
    raster.frontFace              = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth              = 1.0f;
    raster.depthBiasEnable        = VK_TRUE;
    raster.depthBiasConstantFactor = config_.depthBiasConstant;
    raster.depthBiasSlopeFactor   = config_.depthBiasSlope;

    VkPipelineMultisampleStateCreateInfo msaa = {};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo blendState = {};
    blendState.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 0;
    blendState.pAttachments    = nullptr;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState = {};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo pci = {};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState      = &vpState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState   = &msaa;
    pci.pDepthStencilState  = &depthStencil;
    pci.pColorBlendState    = &blendState;
    pci.pDynamicState       = &dynState;
    pci.layout              = pipelineLayout;
    pci.renderPass          = renderPass_;
    pci.subpass             = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_) != VK_SUCCESS) {
        printf("[ShadowMap] Failed to create shadow pipeline\n");
        return false;
    }

    printf("[ShadowMap] Initialized: %ux%u, %d cascades\n", res, res, config_.cascadeCount);
    return true;
}

void ShadowMap::Shutdown(VkDevice device, VmaAllocator allocator) {
    if (pipeline_)   vkDestroyPipeline(device, pipeline_, nullptr);
    if (sampler_)    vkDestroySampler(device, sampler_, nullptr);

    for (int i = 0; i < config_.cascadeCount; i++) {
        if (framebuffers_[i]) vkDestroyFramebuffer(device, framebuffers_[i], nullptr);
        if (sampleViews_[i])  vkDestroyImageView(device, sampleViews_[i], nullptr);
        if (depthViews_[i])   vkDestroyImageView(device, depthViews_[i], nullptr);
        if (images_[i])       vmaDestroyImage(allocator, images_[i], allocs_[i]);
    }

    if (renderPass_) vkDestroyRenderPass(device, renderPass_, nullptr);

    pipeline_   = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    sampler_    = VK_NULL_HANDLE;
}

void ShadowMap::UpdateCascades(const float centerPos[3], const float sunDir[3]) {
    float sx = sunDir[0], sy = sunDir[1], sz = sunDir[2];
    float len = sqrtf(sx * sx + sy * sy + sz * sz);
    if (len < 0.001f) return;
    sx /= len; sy /= len; sz /= len;

    for (int cascade = 0; cascade < config_.cascadeCount; cascade++) {
        float cascadeScale  = (float)(cascade + 1) / (float)config_.cascadeCount;
        float cascadeExtent = config_.shadowRange * cascadeScale;

        float lightDist = 10000.0f;
        float eyeX = centerPos[0] - sx * lightDist;
        float eyeY = centerPos[1] - sy * lightDist;
        float eyeZ = centerPos[2] - sz * lightDist;

        float upX = 0, upY = 1, upZ = 0;
        if (fabsf(sy) > 0.99f) { upX = 0; upY = 0; upZ = 1; }

        float fx = sx, fy = sy, fz = sz;

        float rx = upY * fz - upZ * fy;
        float ry = upZ * fx - upX * fz;
        float rz = upX * fy - upY * fx;
        float rl = sqrtf(rx * rx + ry * ry + rz * rz);
        rx /= rl; ry /= rl; rz /= rl;

        float tux = fy * rz - fz * ry;
        float tuy = fz * rx - fx * rz;
        float tuz = fx * ry - fy * rx;

        float lightView[16] = {
            rx,  tux, fx, 0,
            ry,  tuy, fy, 0,
            rz,  tuz, fz, 0,
            -(rx * eyeX + ry * eyeY + rz * eyeZ),
            -(tux * eyeX + tuy * eyeY + tuz * eyeZ),
            -(fx * eyeX + fy * eyeY + fz * eyeZ),
            1
        };

        float wupt = cascadeExtent / (float)config_.resolution;
        lightView[12] = floorf(lightView[12] / wupt) * wupt;
        lightView[13] = floorf(lightView[13] / wupt) * wupt;

        float ortho[16] = {};
        ortho[0]  = 2.0f / cascadeExtent;
        ortho[5]  = 2.0f / cascadeExtent;
        ortho[10] = 1.0f / 20000.0f;
        ortho[15] = 1.0f;

        float shadowVP[16];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) {
                shadowVP[r * 4 + c] = 0;
                for (int k = 0; k < 4; k++)
                    shadowVP[r * 4 + c] += lightView[r * 4 + k] * ortho[k * 4 + c];
            }

        shadowVP[1]  = -shadowVP[1];
        shadowVP[5]  = -shadowVP[5];
        shadowVP[9]  = -shadowVP[9];
        shadowVP[13] = -shadowVP[13];

        memcpy(vpMatrices_[cascade], shadowVP, 64);
    }
}

} // namespace gvlk
