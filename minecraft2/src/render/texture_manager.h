#pragma once
#include "vulkan_context.h"
#include <volk.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace mc::render {

class TextureManager {
public:
  TextureManager(VulkanContext &context);
  ~TextureManager();

  void LoadTextures(const std::vector<std::string> &textureNames, const std::string& basePath);

  VkImageView GetTextureArrayView() const { return m_textureArrayView; }
  VkSampler GetTextureSampler() const { return m_textureSampler; }

  const std::unordered_map<std::string, int>& GetLayerMap() const { return m_layerMap; }

private:
  void CreateImageArray(uint32_t width, uint32_t height, uint32_t layers);
  void CreateImageView(uint32_t layers);
  void CreateSampler();
  void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount);
  void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);
  
  VkCommandBuffer BeginSingleTimeCommands();
  void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
  void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

  VulkanContext &m_context;

  VkImage m_textureArray = VK_NULL_HANDLE;
  VkDeviceMemory m_textureArrayMemory = VK_NULL_HANDLE;
  VkImageView m_textureArrayView = VK_NULL_HANDLE;
  VkSampler m_textureSampler = VK_NULL_HANDLE;

  VkCommandPool m_commandPool = VK_NULL_HANDLE;

  std::unordered_map<std::string, int> m_layerMap;
};

} // namespace mc::render
