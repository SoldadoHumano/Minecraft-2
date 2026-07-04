#include "vulkan_swapchain.h"
#include "../logzilla/logzilla.h"

namespace mc::render {

VulkanSwapchain::VulkanSwapchain(VulkanContext& context, uint32_t width, uint32_t height)
    : m_context(context), m_width(width), m_height(height) {
  LOGZILLA_INFO("VulkanSwapchain: Creating swapchain (%dx%d)...", width, height);
  CreateSwapchain();
  LOGZILLA_INFO("VulkanSwapchain: Creating image views...");
  CreateImageViews();
  LOGZILLA_INFO("VulkanSwapchain: Creating render pass...");
  CreateRenderPass();
  LOGZILLA_INFO("VulkanSwapchain: Creating depth resources...");
  CreateDepthResources();
  LOGZILLA_INFO("VulkanSwapchain: Creating framebuffers...");
  CreateFramebuffers();
  LOGZILLA_INFO("VulkanSwapchain: Construction complete.");
}

VulkanSwapchain::~VulkanSwapchain() {
  Cleanup();
  vkDestroyRenderPass(m_context.GetDevice(), m_renderPass, nullptr);
}

void VulkanSwapchain::Cleanup() {
  VkDevice device = m_context.GetDevice();
  for (auto framebuffer : m_swapchainFramebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
  for (auto imageView : m_swapchainImageViews) {
    vkDestroyImageView(device, imageView, nullptr);
  }
  if (m_depthImageView) vkDestroyImageView(device, m_depthImageView, nullptr);
  if (m_depthImage) vkDestroyImage(device, m_depthImage, nullptr);
  if (m_depthImageMemory) vkFreeMemory(device, m_depthImageMemory, nullptr);
  if (m_swapchain) vkDestroySwapchainKHR(device, m_swapchain, nullptr);
}

void VulkanSwapchain::Recreate(uint32_t width, uint32_t height) {
  vkDeviceWaitIdle(m_context.GetDevice());
  m_width = width;
  m_height = height;

  Cleanup();

  CreateSwapchain();
  CreateImageViews();
  CreateDepthResources();
  CreateFramebuffers();
}

void VulkanSwapchain::CreateSwapchain() {
  SwapChainSupportDetails swapChainSupport =
      m_context.QuerySwapChainSupport(m_context.GetPhysicalDevice());

  VkSurfaceFormatKHR surfaceFormat = swapChainSupport.formats[0];
  for (const auto &availableFormat : swapChainSupport.formats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      surfaceFormat = availableFormat;
      break;
    }
  }

  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync (fixes high CPU usage)
  for (const auto &availablePresentMode : swapChainSupport.presentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      presentMode = availablePresentMode;
      break;
    }
  }

  VkExtent2D extent = {m_width, m_height};
  extent.width = std::clamp(extent.width,
                            swapChainSupport.capabilities.minImageExtent.width,
                            swapChainSupport.capabilities.maxImageExtent.width);
  extent.height = std::clamp(
      extent.height, swapChainSupport.capabilities.minImageExtent.height,
      swapChainSupport.capabilities.maxImageExtent.height);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = m_context.GetSurface();
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  QueueFamilyIndices indices =
      m_context.FindQueueFamilies(m_context.GetPhysicalDevice());
  uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
                                   indices.presentFamily.value()};

  if (indices.graphicsFamily != indices.presentFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  LOGZILLA_INFO("VulkanSwapchain: vkCreateSwapchainKHR (imageCount=%d, format=%d, extent=%dx%d, presentMode=%d)...",
                imageCount, (int)surfaceFormat.format, extent.width, extent.height, (int)presentMode);
  if (vkCreateSwapchainKHR(m_context.GetDevice(), &createInfo, nullptr,
                           &m_swapchain) != VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain!");
  }
  LOGZILLA_INFO("VulkanSwapchain: vkCreateSwapchainKHR OK, swapchain=%p", (void*)m_swapchain);

  vkGetSwapchainImagesKHR(m_context.GetDevice(), m_swapchain, &imageCount,
                          nullptr);
  m_swapchainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(m_context.GetDevice(), m_swapchain, &imageCount,
                          m_swapchainImages.data());
  LOGZILLA_INFO("VulkanSwapchain: Got %d swapchain images", imageCount);

  m_swapchainImageFormat = surfaceFormat.format;
  m_swapchainExtent = extent;
}

void VulkanSwapchain::CreateImageViews() {
  m_swapchainImageViews.resize(m_swapchainImages.size());
  LOGZILLA_INFO("VulkanSwapchain: Creating %zu image views...", m_swapchainImages.size());
  for (size_t i = 0; i < m_swapchainImages.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = m_swapchainImages[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = m_swapchainImageFormat;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_context.GetDevice(), &createInfo, nullptr,
                          &m_swapchainImageViews[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create image views!");
    }
  }
}

void VulkanSwapchain::CreateRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = m_swapchainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = FindDepthFormat();
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
                                                        depthAttachment};
  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  LOGZILLA_INFO("VulkanSwapchain: vkCreateRenderPass...");
  if (vkCreateRenderPass(m_context.GetDevice(), &renderPassInfo, nullptr,
                         &m_renderPass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
  LOGZILLA_INFO("VulkanSwapchain: vkCreateRenderPass OK, renderPass=%p", (void*)m_renderPass);
}

VkFormat VulkanSwapchain::FindSupportedFormat(const std::vector<VkFormat> &candidates,
                                       VkImageTiling tiling,
                                       VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_context.GetPhysicalDevice(), format,
                                        &props);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }
  throw std::runtime_error("failed to find supported format!");
}

VkFormat VulkanSwapchain::FindDepthFormat() {
  return FindSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void VulkanSwapchain::CreateDepthResources() {
  VkFormat depthFormat = FindDepthFormat();

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = m_swapchainExtent.width;
  imageInfo.extent.height = m_swapchainExtent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = depthFormat;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_context.GetDevice(), &imageInfo, nullptr,
                    &m_depthImage) != VK_SUCCESS) {
    throw std::runtime_error("failed to create depth image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_context.GetDevice(), m_depthImage,
                               &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_context.FindMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr,
                       &m_depthImageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate depth image memory!");
  }

  vkBindImageMemory(m_context.GetDevice(), m_depthImage, m_depthImageMemory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_depthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = depthFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  LOGZILLA_INFO("VulkanSwapchain: Depth image created (%dx%d), depthImage=%p",
                m_swapchainExtent.width, m_swapchainExtent.height, (void*)m_depthImage);

  if (vkCreateImageView(m_context.GetDevice(), &viewInfo, nullptr,
                        &m_depthImageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create depth image view!");
  }
  LOGZILLA_INFO("VulkanSwapchain: Depth image view created, depthImageView=%p", (void*)m_depthImageView);
}

void VulkanSwapchain::CreateFramebuffers() {
  m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
  LOGZILLA_INFO("VulkanSwapchain: Creating %zu framebuffers...", m_swapchainImageViews.size());

  for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
    std::array<VkImageView, 2> attachments = {m_swapchainImageViews[i],
                                              m_depthImageView};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_swapchainExtent.width;
    framebufferInfo.height = m_swapchainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(m_context.GetDevice(), &framebufferInfo, nullptr,
                            &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create framebuffer!");
    }
    LOGZILLA_INFO("VulkanSwapchain: Framebuffer[%zu] created OK", i);
  }
  LOGZILLA_INFO("VulkanSwapchain: All framebuffers created.");
}

} // namespace mc::render
