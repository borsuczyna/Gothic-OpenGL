#include "Pipeline.h"
#include <cstring>
#include <d3d.h>

namespace gvlk {

namespace {

VkCompareOp d3dCmpToVk(DWORD func) {
    switch (func) {
    case D3DCMP_NEVER:        return VK_COMPARE_OP_NEVER;
    case D3DCMP_LESS:         return VK_COMPARE_OP_LESS;
    case D3DCMP_EQUAL:        return VK_COMPARE_OP_EQUAL;
    case D3DCMP_LESSEQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case D3DCMP_GREATER:      return VK_COMPARE_OP_GREATER;
    case D3DCMP_NOTEQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
    case D3DCMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case D3DCMP_ALWAYS:       return VK_COMPARE_OP_ALWAYS;
    default:                  return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

VkBlendFactor d3dBlendToVk(DWORD blend) {
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
    default:                    return VK_BLEND_FACTOR_ONE;
    }
}

} // namespace

VkPipeline PipelineCache::getOrCreate(const PipelineKey& key,
                                      VkDevice device,
                                      VkRenderPass renderPass,
                                      VkPipelineLayout layout,
                                      VkShaderModule vertModule,
                                      VkShaderModule fragModule) {
    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding   = 0;
    binding.stride    = sizeof(GVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[5] = {};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = offsetof(GVertex, x);
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32_UINT;         attrs[1].offset = offsetof(GVertex, color);
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT;    attrs[2].offset = offsetof(GVertex, u);
    attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32G32_SFLOAT;    attrs[3].offset = offsetof(GVertex, u2);
    attrs[4].location = 4; attrs[4].binding = 0; attrs[4].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[4].offset = offsetof(GVertex, wx);

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
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa = {};
    msaa.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = key.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = key.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp   = d3dCmpToVk(key.depthFunc);

    VkPipelineColorBlendAttachmentState blendAtt = {};
    blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAtt.blendEnable         = key.blendEnabled ? VK_TRUE : VK_FALSE;
    blendAtt.srcColorBlendFactor = d3dBlendToVk(key.srcBlend);
    blendAtt.dstColorBlendFactor = d3dBlendToVk(key.dstBlend);
    blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = d3dBlendToVk(key.srcBlend);
    blendAtt.dstAlphaBlendFactor = d3dBlendToVk(key.dstBlend);
    blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blendState = {};
    blendState.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount   = 1;
    blendState.pAttachments      = &blendAtt;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState = {};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount  = 2;
    dynState.pDynamicStates     = dynStates;

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
    pci.layout              = layout;
    pci.renderPass          = renderPass;
    pci.subpass             = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
    if (result != VK_SUCCESS)
        return VK_NULL_HANDLE;

    cache_[key] = pipeline;
    return pipeline;
}

void PipelineCache::clear(VkDevice device) {
    for (auto& [key, pipe] : cache_)
        vkDestroyPipeline(device, pipe, nullptr);
    cache_.clear();
}

} // namespace gvlk
