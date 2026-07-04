#include "vulkan_buffer.h"

namespace mc::render {

void VulkanBuffer::CreateBuffer(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties, VkBuffer &buffer,
                                VkDeviceMemory &bufferMemory) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(context.GetDevice(), &bufferInfo, nullptr, &buffer) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(context.GetDevice(), buffer,
                                &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      context.FindMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(context.GetDevice(), &allocInfo, nullptr,
                       &bufferMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate buffer memory!");
  }

  vkBindBufferMemory(context.GetDevice(), buffer, bufferMemory, 0);
}

void VulkanBuffer::CopyBuffer(VulkanContext& context, VkBuffer srcBuffer, VkBuffer dstBuffer,
                              VkDeviceSize size, VkDeviceSize srcOffset, 
                              VkDeviceSize dstOffset, std::mutex& graphicsQueueMutex) {
  QueueFamilyIndices indices =
      context.FindQueueFamilies(context.GetPhysicalDevice());
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

  VkCommandPool tempPool;
  vkCreateCommandPool(context.GetDevice(), &poolInfo, nullptr, &tempPool);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = tempPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = srcOffset;
  copyRegion.dstOffset = dstOffset;
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence;
  vkCreateFence(context.GetDevice(), &fenceInfo, nullptr, &fence);

  {
    std::lock_guard<std::mutex> lock(graphicsQueueMutex);
    vkQueueSubmit(context.GetGraphicsQueue(), 1, &submitInfo, fence);
  }

  vkWaitForFences(context.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

  vkDestroyFence(context.GetDevice(), fence, nullptr);
  vkFreeCommandBuffers(context.GetDevice(), tempPool, 1, &commandBuffer);
  vkDestroyCommandPool(context.GetDevice(), tempPool, nullptr);
}

} // namespace mc::render
