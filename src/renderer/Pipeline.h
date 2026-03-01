#pragma once

#include "Types.h"
#include "VkWindow.h"
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace gvlk {

class PipelineCache {
public:
    VkPipeline getOrCreate(const PipelineKey& key,
                           VkDevice device,
                           VkRenderPass renderPass,
                           VkPipelineLayout layout,
                           VkShaderModule vertModule,
                           VkShaderModule fragModule);

    void clear(VkDevice device);

private:
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> cache_;
};

} // namespace gvlk
