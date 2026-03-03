#pragma once

#include "VkWindow.h"
#include "Types.h"

namespace gvlk {

class ShadowMap {
public:
    bool Init(VkDevice device, VmaAllocator allocator,
              VkPipelineLayout pipelineLayout,
              VkShaderModule vertModule, VkShaderModule shadowFragModule,
              const ShadowConfig& config);
    void Shutdown(VkDevice device, VmaAllocator allocator);

    void UpdateCascades(const float centerPos[3], const float sunDir[3]);

    VkRenderPass    GetRenderPass() const { return renderPass_; }
    VkPipeline      GetPipeline() const { return pipeline_; }
    VkFramebuffer   GetFramebuffer(int cascade) const { return framebuffers_[cascade]; }
    VkImageView     GetSampleView(int cascade) const { return sampleViews_[cascade]; }
    VkSampler       GetSampler() const { return sampler_; }
    const float*    GetVPMatrix(int cascade) const { return vpMatrices_[cascade]; }
    uint32_t        GetResolution() const { return config_.resolution; }
    int             GetCascadeCount() const { return config_.cascadeCount; }
    bool            IsValid() const { return renderPass_ != VK_NULL_HANDLE; }

private:
    ShadowConfig config_;

    VkRenderPass  renderPass_ = VK_NULL_HANDLE;
    VkPipeline    pipeline_   = VK_NULL_HANDLE;

    VkImage         images_[2]       = {};
    VmaAllocation   allocs_[2]       = {};
    VkImageView     depthViews_[2]   = {};
    VkImageView     sampleViews_[2]  = {};
    VkFramebuffer   framebuffers_[2] = {};

    VkSampler sampler_ = VK_NULL_HANDLE;

    float vpMatrices_[2][16] = {};
};

} // namespace gvlk
