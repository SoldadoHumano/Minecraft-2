#pragma once

#include "vulkan_context.h"
#include <volk.h>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <array>

namespace mc::render {

class VulkanSwapchain {
public:
  VulkanSwapchain(VulkanContext& context, uint32_t width, uint32_t height);
  ~VulkanSwapchain();

  void Recreate(uint32_t width, uint32_t height);

  VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
  VkRenderPass GetRenderPass() const { return m_renderPass; }
  VkExtent2D GetExtent() const { return m_swapchainExtent; }
  VkFormat GetImageFormat() const { return m_swapchainImageFormat; }
  const std::vector<VkImage>& GetImages() const { return m_swapchainImages; }
  const std::vector<VkFramebuffer>& GetFramebuffers() const { return m_swapchainFramebuffers; }
  
private:
  void CreateSwapchain();
  void CreateImageViews();
  void CreateRenderPass();
  void CreateDepthResources();
  void CreateFramebuffers();
  void Cleanup();

  VkFormat FindSupportedFormat(const std::vector<VkFormat> &candidates,
                               VkImageTiling tiling,
                               VkFormatFeatureFlags features);
  VkFormat FindDepthFormat();

  VulkanContext& m_context;
  uint32_t m_width;
  uint32_t m_height;

  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> m_swapchainImages;
  VkFormat m_swapchainImageFormat;
  VkExtent2D m_swapchainExtent;
  std::vector<VkImageView> m_swapchainImageViews;

  VkImage m_depthImage = VK_NULL_HANDLE;
  VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
  VkImageView m_depthImageView = VK_NULL_HANDLE;

  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> m_swapchainFramebuffers;
};

} // namespace mc::render
