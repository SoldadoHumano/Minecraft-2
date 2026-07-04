#include "renderer.h"
#include "shader_compiler.h"
#include <iostream>
#include <stdexcept>
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <algorithm>

namespace mc::render {

const std::string vertShaderCode = R"(
#version 450
layout(location = 0) in uint inData;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    vec4 chunkPos;
} pushConstants;

layout(location = 0) out vec3 fragColor;

vec3 getBlockColor(uint type) {
    if (type == 1) return vec3(0.5, 0.5, 0.5); // Stone
    if (type == 2) return vec3(0.5, 0.3, 0.1); // Dirt
    if (type == 3) return vec3(0.2, 0.8, 0.2); // Grass
    return vec3(1.0, 0.0, 1.0); // Unknown
}

void main() {
    uint x = (inData >> 0) & 0x1F;
    uint y = (inData >> 5) & 0xFF;
    uint z = (inData >> 13) & 0x1F;
    uint face = (inData >> 18) & 0x7;
    uint type = (inData >> 21) & 0xFF;

    vec3 localPos = vec3(float(x), float(y), float(z));
    vec3 worldPos = localPos + pushConstants.chunkPos.xyz;
    
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(worldPos, 1.0);
    
    vec3 color = getBlockColor(type);
    
    if (face == 0 || face == 1) color *= 0.8;
    else if (face == 4 || face == 5) color *= 0.6;
    else if (face == 3) color *= 0.4;
    
    fragColor = color;
}
)";

const std::string fragShaderCode = R"(
#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(fragColor, 1.0);
}
)";

Renderer::Renderer(VulkanContext& context, uint32_t width, uint32_t height)
    : m_context(context), m_width(width), m_height(height) {
    
    ShaderCompiler::Initialize();

    CreateSwapchain();
    CreateImageViews();
    CreateRenderPass();
    CreateDepthResources();
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandPool();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    m_debugUI = std::make_unique<DebugUI>(m_context, m_renderPass, m_width, m_height);
    CreateCommandBuffers();
    CreateSyncObjects();
}

Renderer::~Renderer() {
    VkDevice device = m_context.GetDevice();
    
    vkDeviceWaitIdle(device);
    m_debugUI.reset();

    vkDestroySemaphore(device, m_renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(device, m_imageAvailableSemaphore, nullptr);
    vkDestroyFence(device, m_inFlightFence, nullptr);

    vkDestroyImageView(device, m_depthImageView, nullptr);
    vkDestroyImage(device, m_depthImage, nullptr);
    vkFreeMemory(device, m_depthImageMemory, nullptr);

    vkDestroyCommandPool(device, m_commandPool, nullptr);

    for (auto& pair : m_chunkRenderData) {
        vkDestroyBuffer(device, pair.second.indexBuffer, nullptr);
        vkFreeMemory(device, pair.second.indexBufferMemory, nullptr);
        vkDestroyBuffer(device, pair.second.vertexBuffer, nullptr);
        vkFreeMemory(device, pair.second.vertexBufferMemory, nullptr);
    }
    m_chunkRenderData.clear();

    vkDestroyBuffer(device, m_uniformBuffer, nullptr);
    vkFreeMemory(device, m_uniformBufferMemory, nullptr);

    vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

    for (auto framebuffer : m_swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    vkDestroyPipeline(device, m_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyRenderPass(device, m_renderPass, nullptr);

    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device, m_swapchain, nullptr);

    ShaderCompiler::Finalize();
}

void Renderer::WaitIdle() {
    vkDeviceWaitIdle(m_context.GetDevice());
}

void Renderer::CreateSwapchain() {
    SwapChainSupportDetails swapChainSupport = m_context.QuerySwapChainSupport(m_context.GetPhysicalDevice());

    VkSurfaceFormatKHR surfaceFormat = swapChainSupport.formats[0];
    for (const auto& availableFormat : swapChainSupport.formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = availableFormat;
            break;
        }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // Fallback
    for (const auto& availablePresentMode : swapChainSupport.presentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) { // Uncapped FPS
            presentMode = availablePresentMode;
            break;
        }
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR && presentMode != VK_PRESENT_MODE_IMMEDIATE_KHR) {
            presentMode = availablePresentMode;
        }
    }

    VkExtent2D extent = { m_width, m_height };
    extent.width = std::clamp(extent.width, swapChainSupport.capabilities.minImageExtent.width, swapChainSupport.capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, swapChainSupport.capabilities.minImageExtent.height, swapChainSupport.capabilities.maxImageExtent.height);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
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

    QueueFamilyIndices indices = m_context.FindQueueFamilies(m_context.GetPhysicalDevice());
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

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

    if (vkCreateSwapchainKHR(m_context.GetDevice(), &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_context.GetDevice(), m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_context.GetDevice(), m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
}

void Renderer::CreateImageViews() {
    m_swapchainImageViews.resize(m_swapchainImages.size());
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

        if (vkCreateImageView(m_context.GetDevice(), &createInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }
    }
}

void Renderer::CreateRenderPass() {
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
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_context.GetDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

VkFormat Renderer::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_context.GetPhysicalDevice(), format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("failed to find supported format!");
}

VkFormat Renderer::FindDepthFormat() {
    return FindSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

void Renderer::CreateDepthResources() {
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
    
    if (vkCreateImage(m_context.GetDevice(), &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.GetDevice(), m_depthImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context.FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS) {
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
    
    if (vkCreateImageView(m_context.GetDevice(), &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth image view!");
    }
}

void Renderer::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_context.GetDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

VkShaderModule Renderer::CreateShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_context.GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

void Renderer::CreateGraphicsPipeline() {
    auto vertSpv = ShaderCompiler::CompileGLSLToSPIRV(vertShaderCode, VK_SHADER_STAGE_VERTEX_BIT);
    auto fragSpv = ShaderCompiler::CompileGLSLToSPIRV(fragShaderCode, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkShaderModule vertShaderModule = CreateShaderModule(vertSpv);
    VkShaderModule fragShaderModule = CreateShaderModule(fragSpv);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32_UINT;
    attributeDescriptions[0].offset = offsetof(Vertex, data);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) m_swapchainExtent.width;
    viewport.height = (float) m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::vec4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_context.GetDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_context.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(m_context.GetDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_context.GetDevice(), vertShaderModule, nullptr);
}

void Renderer::CreateFramebuffers() {
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            m_swapchainImageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_context.GetDevice(), &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void Renderer::CreateCommandPool() {
    QueueFamilyIndices queueFamilyIndices = m_context.FindQueueFamilies(m_context.GetPhysicalDevice());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_context.GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void Renderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
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

void Renderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    // Create a temporary command pool for this thread to avoid locking and driver race conditions
    QueueFamilyIndices indices = m_context.FindQueueFamilies(m_context.GetPhysicalDevice());
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();
    
    VkCommandPool tempPool;
    vkCreateCommandPool(m_context.GetDevice(), &poolInfo, nullptr, &tempPool);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = tempPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_context.GetDevice(), &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
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
    vkCreateFence(m_context.GetDevice(), &fenceInfo, nullptr, &fence);

    {
        std::lock_guard<std::mutex> lock(m_graphicsQueueMutex);
        vkQueueSubmit(m_context.GetGraphicsQueue(), 1, &submitInfo, fence);
    }

    vkWaitForFences(m_context.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
    
    vkDestroyFence(m_context.GetDevice(), fence, nullptr);
    vkFreeCommandBuffers(m_context.GetDevice(), tempPool, 1, &commandBuffer);
    vkDestroyCommandPool(m_context.GetDevice(), tempPool, nullptr);
}



void Renderer::CreateUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_uniformBuffer, m_uniformBufferMemory);
    vkMapMemory(m_context.GetDevice(), m_uniformBufferMemory, 0, bufferSize, 0, &m_uniformBufferMapped);
}

void Renderer::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(m_context.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void Renderer::CreateDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(m_context.GetDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_context.GetDevice(), 1, &descriptorWrite, 0, nullptr);
}

void Renderer::CreateCommandBuffers() {
    m_commandBuffers.resize(1); // One command buffer for simplicity in this milestone

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_context.GetDevice(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void Renderer::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(m_context.GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_context.GetDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(m_context.GetDevice(), &fenceInfo, nullptr, &m_inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create synchronization objects!");
    }
}

void Renderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const mc::core::Camera& camera, float time, mc::core::EngineMetrics& metrics) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.5f, 0.7f, 1.0f, 1.0f}}; // Sky blue clear color
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {m_width, m_height};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // Frustum Culling Math
    glm::mat4 vp = camera.GetProjectionMatrix() * camera.GetViewMatrix();
    glm::vec4 planes[6];
    planes[0].x = vp[0][3] + vp[0][0]; planes[0].y = vp[1][3] + vp[1][0]; planes[0].z = vp[2][3] + vp[2][0]; planes[0].w = vp[3][3] + vp[3][0]; // Left
    planes[1].x = vp[0][3] - vp[0][0]; planes[1].y = vp[1][3] - vp[1][0]; planes[1].z = vp[2][3] - vp[2][0]; planes[1].w = vp[3][3] - vp[3][0]; // Right
    planes[2].x = vp[0][3] + vp[0][1]; planes[2].y = vp[1][3] + vp[1][1]; planes[2].z = vp[2][3] + vp[2][1]; planes[2].w = vp[3][3] + vp[3][1]; // Bottom
    planes[3].x = vp[0][3] - vp[0][1]; planes[3].y = vp[1][3] - vp[1][1]; planes[3].z = vp[2][3] - vp[2][1]; planes[3].w = vp[3][3] - vp[3][1]; // Top
    planes[4].x = vp[0][3] + vp[0][2]; planes[4].y = vp[1][3] + vp[1][2]; planes[4].z = vp[2][3] + vp[2][2]; planes[4].w = vp[3][3] + vp[3][2]; // Near
    planes[5].x = vp[0][3] - vp[0][2]; planes[5].y = vp[1][3] - vp[1][2]; planes[5].z = vp[2][3] - vp[2][2]; planes[5].w = vp[3][3] - vp[3][2]; // Far

    for (int i = 0; i < 6; ++i) {
        float length = std::sqrt(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
        planes[i] /= length;
    }

    metrics.verticesRendered = 0;
    metrics.chunksRendered = 0;
    metrics.chunksLoaded = static_cast<uint32_t>(m_chunkRenderData.size());

    for (const auto& pair : m_chunkRenderData) {
        const auto& chunk = pair.second;
        if (chunk.indexCount == 0) continue;

        // Frustum AABB test using chunk's actual geometry bounds
        glm::vec3 minAABB(chunk.x * 16.0f, chunk.minY, chunk.z * 16.0f);
        glm::vec3 maxAABB(chunk.x * 16.0f + 16.0f, chunk.maxY, chunk.z * 16.0f + 16.0f);
        
        bool inside = true;
        for (int i = 0; i < 6; ++i) {
            glm::vec3 p = minAABB;
            if (planes[i].x >= 0) p.x = maxAABB.x;
            if (planes[i].y >= 0) p.y = maxAABB.y;
            if (planes[i].z >= 0) p.z = maxAABB.z;
            
            if (planes[i].x * p.x + planes[i].y * p.y + planes[i].z * p.z + planes[i].w < 0) {
                inside = false;
                break;
            }
        }
        
        if (!inside) continue;
        
        metrics.chunksRendered++;
        metrics.verticesRendered += chunk.indexCount; // Storing index count roughly as vertices since it's the geometry processed

        VkBuffer vertexBuffers[] = {chunk.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        glm::vec4 pushData(chunk.x * 16.0f, chunk.y * 16.0f, chunk.z * 16.0f, 0.0f);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec4), &pushData);

        vkCmdDrawIndexed(commandBuffer, chunk.indexCount, 1, 0, 0, 0);
    }

    m_debugUI->Draw(commandBuffer, metrics);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

bool Renderer::DrawFrame(const mc::core::Camera& camera, float time, mc::core::EngineMetrics& metrics) {
    vkWaitForFences(m_context.GetDevice(), 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    
    // Safe to process pending deletions because GPU is idle for this frame
    std::vector<glm::ivec3> toDelete;
    {
        std::lock_guard<std::mutex> lock(m_deletionQueueMutex);
        toDelete = std::move(m_deletionQueue);
        m_deletionQueue.clear();
    }
    
    for (const auto& pos : toDelete) {
        auto it = m_chunkRenderData.find(pos);
        if (it != m_chunkRenderData.end()) {
            vkDestroyBuffer(m_context.GetDevice(), it->second.indexBuffer, nullptr);
            vkFreeMemory(m_context.GetDevice(), it->second.indexBufferMemory, nullptr);
            vkDestroyBuffer(m_context.GetDevice(), it->second.vertexBuffer, nullptr);
            vkFreeMemory(m_context.GetDevice(), it->second.vertexBufferMemory, nullptr);
            m_chunkRenderData.erase(it);
        }
    }
    
    ProcessPendingChunks();

    vkResetFences(m_context.GetDevice(), 1, &m_inFlightFence);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_context.GetDevice(), m_swapchain, UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = camera.GetViewMatrix();
    ubo.proj = camera.GetProjectionMatrix();
    memcpy(m_uniformBufferMapped, &ubo, sizeof(ubo));

    vkResetCommandBuffer(m_commandBuffers[0], 0);
    RecordCommandBuffer(m_commandBuffers[0], imageIndex, camera, time, metrics);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[0];

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    {
        std::lock_guard<std::mutex> lock(m_graphicsQueueMutex);
        if (vkQueueSubmit(m_context.GetGraphicsQueue(), 1, &submitInfo, m_inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer!");
        }
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult resultPresent;
    {
        std::lock_guard<std::mutex> lock(m_graphicsQueueMutex);
        resultPresent = vkQueuePresentKHR(m_context.GetPresentQueue(), &presentInfo);
    }

    if (resultPresent == VK_ERROR_OUT_OF_DATE_KHR || resultPresent == VK_SUBOPTIMAL_KHR) {
        return false;
    } else if (resultPresent != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    return true;
}

void Renderer::RecreateSwapchain(int width, int height) {
    vkDeviceWaitIdle(m_context.GetDevice());

    m_width = width;
    m_height = height;
    
    if (m_debugUI) {
        m_debugUI->Resize(width, height);
    }

    for (auto framebuffer : m_swapchainFramebuffers) {
        vkDestroyFramebuffer(m_context.GetDevice(), framebuffer, nullptr);
    }
    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_context.GetDevice(), imageView, nullptr);
    }
    vkDestroyImageView(m_context.GetDevice(), m_depthImageView, nullptr);
    vkDestroyImage(m_context.GetDevice(), m_depthImage, nullptr);
    vkFreeMemory(m_context.GetDevice(), m_depthImageMemory, nullptr);
    vkDestroySwapchainKHR(m_context.GetDevice(), m_swapchain, nullptr);

    CreateSwapchain();
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();
}

void Renderer::UnloadChunk(int cx, int cz) {
    std::lock_guard<std::mutex> lock(m_deletionQueueMutex);
    for (int y = 0; y < 16; ++y) {
        m_deletionQueue.push_back(glm::ivec3(cx, y, cz));
    }
}

void Renderer::ProcessPendingChunks() {
    std::lock_guard<std::mutex> lock(m_readyRenderDataMutex);
    for (const auto& data : m_readyRenderData) {
        glm::ivec3 pos(data.x, data.y, data.z);
        
        // If there was an old chunk at this position, we should clean it up
        auto it = m_chunkRenderData.find(pos);
        if (it != m_chunkRenderData.end()) {
            vkDestroyBuffer(m_context.GetDevice(), it->second.indexBuffer, nullptr);
            vkFreeMemory(m_context.GetDevice(), it->second.indexBufferMemory, nullptr);
            vkDestroyBuffer(m_context.GetDevice(), it->second.vertexBuffer, nullptr);
            vkFreeMemory(m_context.GetDevice(), it->second.vertexBufferMemory, nullptr);
        }
        
        m_chunkRenderData[pos] = data;
    }
    m_readyRenderData.clear();
}

void Renderer::UploadMeshAsync(const mc::world::ChunkMesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return;
    
    ChunkRenderData renderData;
    renderData.x = mesh.x;
    renderData.y = mesh.y;
    renderData.z = mesh.z;
    renderData.minY = mesh.minY;
    renderData.maxY = mesh.maxY;
    renderData.indexCount = static_cast<uint32_t>(mesh.indices.size());
    
    // Vertex Buffer
    VkDeviceSize vertexBufferSize = sizeof(mesh.vertices[0]) * mesh.vertices.size();
    VkBuffer vertexStagingBuffer;
    VkDeviceMemory vertexStagingBufferMemory;
    CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexStagingBuffer, vertexStagingBufferMemory);
    void* vData;
    vkMapMemory(m_context.GetDevice(), vertexStagingBufferMemory, 0, vertexBufferSize, 0, &vData);
    memcpy(vData, mesh.vertices.data(), (size_t)vertexBufferSize);
    vkUnmapMemory(m_context.GetDevice(), vertexStagingBufferMemory);
    CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderData.vertexBuffer, renderData.vertexBufferMemory);
    CopyBuffer(vertexStagingBuffer, renderData.vertexBuffer, vertexBufferSize);
    vkDestroyBuffer(m_context.GetDevice(), vertexStagingBuffer, nullptr);
    vkFreeMemory(m_context.GetDevice(), vertexStagingBufferMemory, nullptr);
    
    // Index Buffer
    VkDeviceSize indexBufferSize = sizeof(mesh.indices[0]) * mesh.indices.size();
    VkBuffer indexStagingBuffer;
    VkDeviceMemory indexStagingBufferMemory;
    CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexStagingBuffer, indexStagingBufferMemory);
    void* iData;
    vkMapMemory(m_context.GetDevice(), indexStagingBufferMemory, 0, indexBufferSize, 0, &iData);
    memcpy(iData, mesh.indices.data(), (size_t)indexBufferSize);
    vkUnmapMemory(m_context.GetDevice(), indexStagingBufferMemory);
    CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderData.indexBuffer, renderData.indexBufferMemory);
    CopyBuffer(indexStagingBuffer, renderData.indexBuffer, indexBufferSize);
    vkDestroyBuffer(m_context.GetDevice(), indexStagingBuffer, nullptr);
    vkFreeMemory(m_context.GetDevice(), indexStagingBufferMemory, nullptr);
    
    std::lock_guard<std::mutex> lock(m_readyRenderDataMutex);
    m_readyRenderData.push_back(renderData);
}

} // namespace mc::render

bool mc::render::Renderer::HandleClick(double x, double y, mc::core::EngineMetrics& metrics) {
    if (m_debugUI) {
        return m_debugUI->HandleClick(x, y, metrics);
    }
    return false;
}

void mc::render::Renderer::HandleDrag(double x, double y, mc::core::EngineMetrics& metrics) {
    if (m_debugUI) {
        m_debugUI->HandleDrag(x, y, metrics);
    }
}
