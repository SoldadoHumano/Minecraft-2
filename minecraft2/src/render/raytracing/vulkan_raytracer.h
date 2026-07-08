#pragma once

#include "render/vulkan_context.h"
#include <volk.h>
#include <glm/glm.hpp>
#include <vector>

namespace mc::render {

class VulkanRaytracer {
public:
  VulkanRaytracer(VulkanContext& context, VkCommandPool commandPool);
  ~VulkanRaytracer();

  void Init(int width, int height, VkImageView textureArrayView, VkSampler textureSampler);
  void Shutdown();
  void Resize(int width, int height);

  void UpdateVoxelGrid(const std::vector<uint8_t>& flattenedBlocks, int sizeX, int sizeY, int sizeZ, int chunkOffsetX, int chunkOffsetZ);
  void UpdateBlockLayers();
  
  // The compute shader writes directly to a storage image
  void RenderFrame(VkCommandBuffer cmdBuf, const glm::mat4& invView, const glm::mat4& invProj, const glm::vec3& cameraPos, const glm::vec3& sunDir);

  VkImageView GetOutputImageView() const { return m_outputImageView; }
  VkSampler GetOutputSampler() const { return m_outputSampler; }

private:
  void CreateComputePipeline();
  void CreateOutputImage(int width, int height);
  void CreateDescriptorSets();

  VulkanContext& m_context;
  VkCommandPool m_commandPool = VK_NULL_HANDLE;
  int m_width = 0;
  int m_height = 0;

  VkCommandBuffer BeginSingleTimeCommands();
  void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_computePipeline = VK_NULL_HANDLE;

  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

  VkImage m_outputImage = VK_NULL_HANDLE;
  VkDeviceMemory m_outputImageMemory = VK_NULL_HANDLE;
  VkImageView m_outputImageView = VK_NULL_HANDLE;
  VkSampler m_outputSampler = VK_NULL_HANDLE;

  VkImageView m_textureArrayView = VK_NULL_HANDLE;
  VkSampler m_textureSampler = VK_NULL_HANDLE;

  VkBuffer m_voxelGridBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_voxelGridMemory = VK_NULL_HANDLE;
  VkDeviceSize m_voxelGridBufferSize = 0;

  VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
  VkDeviceSize m_stagingBufferSize = 0;

  VkBuffer m_blockLayersBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_blockLayersMemory = VK_NULL_HANDLE;
  VkDeviceSize m_blockLayersBufferSize = 0;

  struct UniformData {
    glm::mat4 invView;
    glm::mat4 invProj;
    glm::vec4 cameraPos; // .w is gridWidth
    glm::vec4 sunDir;    // .w is gridHeight
    glm::ivec4 gridData; // x = gridDepth, y = gridOffsetX, z = gridOffsetZ, w = padding
  };

  VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_uniformBufferMemory = VK_NULL_HANDLE;
  void* m_uniformMapped = nullptr;
};

} // namespace mc::render
