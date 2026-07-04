#pragma once

#include "vulkan_context.h"
#include <volk.h>
#include <stdexcept>
#include <mutex>

namespace mc::render {

class VulkanBuffer {
public:
  static void CreateBuffer(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties, VkBuffer &buffer,
                           VkDeviceMemory &bufferMemory);

  static void CopyBuffer(VulkanContext& context, VkBuffer srcBuffer, VkBuffer dstBuffer,
                         VkDeviceSize size, VkDeviceSize srcOffset, 
                         VkDeviceSize dstOffset, std::mutex& graphicsQueueMutex);
};

} // namespace mc::render
