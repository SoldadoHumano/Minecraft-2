#include "texture_manager.h"
#include "../logzilla/logzilla.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"

#include <stdexcept>

namespace mc::render {

TextureManager::TextureManager(VulkanContext &context) : m_context(context) {
  // Create command pool for single-time commands
  QueueFamilyIndices queueFamilyIndices =
      m_context.FindQueueFamilies(m_context.GetPhysicalDevice());

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

  if (vkCreateCommandPool(m_context.GetDevice(), &poolInfo, nullptr,
                          &m_commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture command pool!");
  }
}

TextureManager::~TextureManager() {
  VkDevice device = m_context.GetDevice();
  if (m_textureSampler != VK_NULL_HANDLE)
    vkDestroySampler(device, m_textureSampler, nullptr);
  if (m_textureArrayView != VK_NULL_HANDLE)
    vkDestroyImageView(device, m_textureArrayView, nullptr);
  if (m_textureArray != VK_NULL_HANDLE)
    vkDestroyImage(device, m_textureArray, nullptr);
  if (m_textureArrayMemory != VK_NULL_HANDLE)
    vkFreeMemory(device, m_textureArrayMemory, nullptr);
  if (m_commandPool != VK_NULL_HANDLE)
    vkDestroyCommandPool(device, m_commandPool, nullptr);
}

void TextureManager::LoadTextures(const std::vector<std::string> &textureNames,
                                  const std::string &basePath) {
  if (textureNames.empty())
    return;

  int texWidth = 32, texHeight = 32, texChannels = 4;
  uint32_t layerCount = static_cast<uint32_t>(textureNames.size());
  VkDeviceSize layerSize = texWidth * texHeight * 4;
  VkDeviceSize imageSize = layerSize * layerCount;

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void *data;
  vkMapMemory(m_context.GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);

  for (size_t i = 0; i < textureNames.size(); ++i) {
    std::string fullPath = basePath + "/" + textureNames[i];
    int width, height, channels;
    stbi_uc *pixels =
        stbi_load(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
      LOGZILLA_WARN("Failed to load texture image: %s", fullPath.c_str());
      // Fill with purple/black checkerboard instead of crashing
      memset((char*)data + (layerSize * i), 255, layerSize); // Just white for missing
    } else {
      if (width != texWidth || height != texHeight) {
        LOGZILLA_WARN("Texture %s has wrong dimensions (%dx%d), expected %dx%d!",
                      fullPath.c_str(), width, height, texWidth, texHeight);
      }
      memcpy(static_cast<char *>(data) + (layerSize * i), pixels, layerSize);
      stbi_image_free(pixels);
    }
    
    m_layerMap[textureNames[i]] = static_cast<int>(i);
  }
  vkUnmapMemory(m_context.GetDevice(), stagingBufferMemory);

  CreateImageArray(texWidth, texHeight, layerCount);
  
  TransitionImageLayout(m_textureArray, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount);
                        
  CopyBufferToImage(stagingBuffer, m_textureArray, static_cast<uint32_t>(texWidth),
                    static_cast<uint32_t>(texHeight), layerCount);
                    
  TransitionImageLayout(m_textureArray, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerCount);

  vkDestroyBuffer(m_context.GetDevice(), stagingBuffer, nullptr);
  vkFreeMemory(m_context.GetDevice(), stagingBufferMemory, nullptr);

  CreateImageView(layerCount);
  CreateSampler();
}

void TextureManager::CreateImageArray(uint32_t width, uint32_t height, uint32_t layers) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = layers;
  imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_context.GetDevice(), &imageInfo, nullptr, &m_textureArray) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture array image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_context.GetDevice(), m_textureArray, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_context.FindMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr, &m_textureArrayMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate texture array image memory!");
  }

  vkBindImageMemory(m_context.GetDevice(), m_textureArray, m_textureArrayMemory, 0);
}

void TextureManager::CreateImageView(uint32_t layers) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_textureArray;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = layers;

  if (vkCreateImageView(m_context.GetDevice(), &viewInfo, nullptr, &m_textureArrayView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture array image view!");
  }
}

void TextureManager::CreateSampler() {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_NEAREST; // Pixel art style
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  
  samplerInfo.anisotropyEnable = VK_FALSE; // Disable for pixel art
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  if (vkCreateSampler(m_context.GetDevice(), &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}

void TextureManager::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount) {
  VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = layerCount;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  vkCmdPipelineBarrier(
      commandBuffer,
      sourceStage, destinationStage,
      0,
      0, nullptr,
      0, nullptr,
      1, &barrier
  );

  EndSingleTimeCommands(commandBuffer);
}

void TextureManager::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) {
  VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = layerCount;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  EndSingleTimeCommands(commandBuffer);
}

VkCommandBuffer TextureManager::BeginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = m_commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(m_context.GetDevice(), &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void TextureManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(m_context.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_context.GetGraphicsQueue());

  vkFreeCommandBuffers(m_context.GetDevice(), m_commandPool, 1, &commandBuffer);
}

void TextureManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context.GetDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.GetDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context.FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_context.GetDevice(), buffer, bufferMemory, 0);
}

} // namespace mc::render
