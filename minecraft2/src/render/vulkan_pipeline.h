#pragma once

#include "vulkan_context.h"
#include <volk.h>
#include <vector>
#include <stdexcept>
#include <glm/glm.hpp>

namespace mc::render {

struct PipelineObjects {
  VkPipelineLayout layout;
  VkPipeline pipeline;
};

class VulkanPipeline {
public:
  static PipelineObjects CreateGraphicsPipeline(VulkanContext& context, 
                                                VkRenderPass renderPass, 
                                                VkDescriptorSetLayout descriptorSetLayout,
                                                VkExtent2D extent);

  static PipelineObjects CreateAABBPipeline(VulkanContext& context,
                                            VkRenderPass renderPass,
                                            VkDescriptorSetLayout descriptorSetLayout);

  static PipelineObjects CreateComputePipeline(VulkanContext& context,
                                               VkDescriptorSetLayout descriptorSetLayout,
                                               const std::vector<uint32_t>& computeCode);

private:
  static VkShaderModule CreateShaderModule(VulkanContext& context, const std::vector<uint32_t> &code);
};

} // namespace mc::render
