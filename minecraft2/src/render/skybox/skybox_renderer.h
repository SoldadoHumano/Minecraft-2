#pragma once
#include "../vulkan_context.h"
#include "../vulkan_pipeline.h"
#include <memory>
#include <glm/glm.hpp>

namespace mc::render {

class SkyboxRenderer {
public:
    SkyboxRenderer(VulkanContext& context, VkRenderPass renderPass);
    ~SkyboxRenderer();

    void Draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj);

private:
    void GenerateTexture();
    void CreatePipeline(VkRenderPass renderPass);

    VulkanContext& m_context;
    
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    
    VkImage m_sunImage = VK_NULL_HANDLE;
    VkDeviceMemory m_sunImageMemory = VK_NULL_HANDLE;
    VkImageView m_sunImageView = VK_NULL_HANDLE;
    VkSampler m_sunSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace mc::render
