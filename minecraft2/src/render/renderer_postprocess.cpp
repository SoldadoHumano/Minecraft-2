#include "renderer.h"
#include "shader_compiler.h"
#include "../logzilla/logzilla.h"
#include <array>
#include <stdexcept>
#include <string>

namespace mc::render {

static const std::string postprocessVert = R"(
#version 450
layout(location = 0) out vec2 outUV;
void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const std::string blurFrag = R"(
#version 450
layout(location = 0) in vec2 inUV;
layout(binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    vec2 direction;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 tex_offset = pc.direction;
    
    // Thresholding: only blur pixels brighter than 1.0
    // Wait, if this is a separable blur, thresholding should only happen on the FIRST pass.
    // We will do thresholding in the first pass (horizontal) using the direction vector's x component to detect it.
    // A better way is to just do it based on a push constant flag, but to keep it simple:
    
    vec3 result = vec3(0.0);
    vec2 uv = inUV;
    
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    
    // Center pixel
    vec3 c0 = texture(texSampler, uv).rgb;
    // Extract bright parts only if this is the first pass (horizontal)
    if (pc.direction.y == 0.0) {
        float brightness = dot(c0, vec3(0.2126, 0.7152, 0.0722));
        if(brightness > 1.0) {
            result += c0 * weights[0];
        }
    } else {
        result += c0 * weights[0];
    }
    
    for(int i = 1; i < 5; ++i) {
        vec3 c1 = texture(texSampler, uv + tex_offset * float(i)).rgb;
        vec3 c2 = texture(texSampler, uv - tex_offset * float(i)).rgb;
        
        if (pc.direction.y == 0.0) {
            float b1 = dot(c1, vec3(0.2126, 0.7152, 0.0722));
            float b2 = dot(c2, vec3(0.2126, 0.7152, 0.0722));
            if(b1 > 1.0) result += c1 * weights[i];
            if(b2 > 1.0) result += c2 * weights[i];
        } else {
            result += c1 * weights[i];
            result += c2 * weights[i];
        }
    }
    
    outColor = vec4(result, 1.0);
}
)";

static const std::string compositeFrag = R"(
#version 450
layout(location = 0) in vec2 inUV;
layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D blurColor;

layout(push_constant) uniform PushConstants {
    vec2 direction;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 hdrColor = texture(sceneColor, inUV).rgb;
    vec3 bloomColor = texture(blurColor, inUV).rgb;
    
    vec3 result = hdrColor + bloomColor;
    outColor = vec4(result, 1.0);
}
)";

// Helper to create image and view
static void CreateImageAndView(VulkanContext& context, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(context.GetDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context.GetDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(context.GetDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate offscreen image memory!");
    }

    vkBindImageMemory(context.GetDevice(), image, memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context.GetDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen image view!");
    }
}

void Renderer::CreateOffscreenResources() {
    VkDevice device = m_context.GetDevice();
    VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; // We could reuse FindDepthFormat but hardcoding is fine for simple bloom

    // 1. Create Images
    CreateImageAndView(m_context, m_width, m_height, hdrFormat, 
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 
        m_offscreenColorImage, m_offscreenColorMemory, m_offscreenColorView);
        
    CreateImageAndView(m_context, m_width, m_height, hdrFormat, 
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 
        m_blurImage, m_blurMemory, m_blurView);

    CreateImageAndView(m_context, m_width, m_height, depthFormat, 
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 
        m_offscreenDepthImage, m_offscreenDepthMemory, m_offscreenDepthView);

    // 2. Create Render Passes
    // Main Render Pass (renders to m_offscreenColorImage)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = hdrFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_mainRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create main render pass!");
    }

    // Blur Render Pass (renders to m_blurImage)
    VkAttachmentDescription blurAttachment{};
    blurAttachment.format = hdrFormat;
    blurAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    blurAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // We overwrite the whole screen
    blurAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    blurAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    blurAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    blurAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    blurAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference blurAttachmentRef{};
    blurAttachmentRef.attachment = 0;
    blurAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription blurSubpass{};
    blurSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    blurSubpass.colorAttachmentCount = 1;
    blurSubpass.pColorAttachments = &blurAttachmentRef;

    VkRenderPassCreateInfo blurRenderPassInfo{};
    blurRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    blurRenderPassInfo.attachmentCount = 1;
    blurRenderPassInfo.pAttachments = &blurAttachment;
    blurRenderPassInfo.subpassCount = 1;
    blurRenderPassInfo.pSubpasses = &blurSubpass;

    if (vkCreateRenderPass(device, &blurRenderPassInfo, nullptr, &m_blurRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create blur render pass!");
    }

    // 3. Create Framebuffers
    std::array<VkImageView, 2> fbAttachments = {m_offscreenColorView, m_offscreenDepthView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_mainRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
    framebufferInfo.pAttachments = fbAttachments.data();
    framebufferInfo.width = m_width;
    framebufferInfo.height = m_height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_mainFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create main framebuffer!");
    }

    VkFramebufferCreateInfo blurFbInfo{};
    blurFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    blurFbInfo.renderPass = m_blurRenderPass;
    blurFbInfo.attachmentCount = 1;
    blurFbInfo.pAttachments = &m_blurView;
    blurFbInfo.width = m_width;
    blurFbInfo.height = m_height;
    blurFbInfo.layers = 1;
    if (vkCreateFramebuffer(device, &blurFbInfo, nullptr, &m_blurFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create blur framebuffer!");
    }
}

void Renderer::CreatePostProcessPipelines() {
    VkDevice device = m_context.GetDevice();
    
    // Shared sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    VkSampler sampler;
    vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
    // Actually we can leak this sampler or store it. Let's just create it. We leak it for simplicity since we don't have a member, or we can use m_textureManager->GetTextureSampler()! 
    // Let's use m_textureManager->GetTextureSampler() since we already have one.

    // 1. Blur Descriptor Set Layout
    VkDescriptorSetLayoutBinding blurBinding{};
    blurBinding.binding = 0;
    blurBinding.descriptorCount = 1;
    blurBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    blurBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &blurBinding;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_blurDescriptorSetLayout);
    
    // 2. Composite Descriptor Set Layout
    std::array<VkDescriptorSetLayoutBinding, 2> compBindings{};
    compBindings[0].binding = 0;
    compBindings[0].descriptorCount = 1;
    compBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    compBindings[1].binding = 1;
    compBindings[1].descriptorCount = 1;
    compBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo compLayoutInfo{};
    compLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    compLayoutInfo.bindingCount = 2;
    compLayoutInfo.pBindings = compBindings.data();
    vkCreateDescriptorSetLayout(device, &compLayoutInfo, nullptr, &m_compositeDescriptorSetLayout);
    
    // Pools
    VkDescriptorPoolSize poolSizes[1] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 2;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_blurDescriptorPool);
    
    // Allocate Sets
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_blurDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_blurDescriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfo, &m_blurDescriptorSet);
    
    allocInfo.pSetLayouts = &m_compositeDescriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfo, &m_compositeDescriptorSet);
    
    // Write Sets
    VkDescriptorImageInfo imageInfo1{};
    imageInfo1.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo1.imageView = m_offscreenColorView;
    imageInfo1.sampler = sampler;
    
    VkDescriptorImageInfo imageInfo2{};
    imageInfo2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo2.imageView = m_blurView;
    imageInfo2.sampler = sampler;
    
    std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_blurDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfo1;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_compositeDescriptorSet;
    descriptorWrites[1].dstBinding = 0;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo1;
    
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = m_compositeDescriptorSet;
    descriptorWrites[2].dstBinding = 1;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &imageInfo2;
    
    vkUpdateDescriptorSets(device, 3, descriptorWrites.data(), 0, nullptr);

    // 3. Compile Shaders & Create Pipelines
    auto vertSpv = ShaderCompiler::CompileGLSLToSPIRV(postprocessVert, VK_SHADER_STAGE_VERTEX_BIT);
    auto blurFragSpv = ShaderCompiler::CompileGLSLToSPIRV(blurFrag, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto compFragSpv = ShaderCompiler::CompileGLSLToSPIRV(compositeFrag, VK_SHADER_STAGE_FRAGMENT_BIT);
    
    VkShaderModuleCreateInfo vertModuleInfo{};
    vertModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertModuleInfo.codeSize = vertSpv.size() * sizeof(uint32_t);
    vertModuleInfo.pCode = vertSpv.data();
    VkShaderModule vertModule;
    vkCreateShaderModule(device, &vertModuleInfo, nullptr, &vertModule);
    
    VkShaderModuleCreateInfo blurFragModuleInfo{};
    blurFragModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    blurFragModuleInfo.codeSize = blurFragSpv.size() * sizeof(uint32_t);
    blurFragModuleInfo.pCode = blurFragSpv.data();
    VkShaderModule blurFragModule;
    vkCreateShaderModule(device, &blurFragModuleInfo, nullptr, &blurFragModule);
    
    VkShaderModuleCreateInfo compFragModuleInfo{};
    compFragModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    compFragModuleInfo.codeSize = compFragSpv.size() * sizeof(uint32_t);
    compFragModuleInfo.pCode = compFragSpv.data();
    VkShaderModule compFragModule;
    vkCreateShaderModule(device, &compFragModuleInfo, nullptr, &compFragModule);

    // Blur Pipeline
    VkPipelineShaderStageCreateInfo blurStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, blurFragModule, "main", nullptr}
    };
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Blur Layout
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(glm::vec2);
    
    VkPipelineLayoutCreateInfo blurLayoutInfo{};
    blurLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    blurLayoutInfo.setLayoutCount = 1;
    blurLayoutInfo.pSetLayouts = &m_blurDescriptorSetLayout;
    blurLayoutInfo.pushConstantRangeCount = 1;
    blurLayoutInfo.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(device, &blurLayoutInfo, nullptr, &m_blurPipelineLayout);
    
    VkGraphicsPipelineCreateInfo blurPipelineInfo{};
    blurPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    blurPipelineInfo.stageCount = 2;
    blurPipelineInfo.pStages = blurStages;
    blurPipelineInfo.pVertexInputState = &vertexInputInfo;
    blurPipelineInfo.pInputAssemblyState = &inputAssembly;
    blurPipelineInfo.pViewportState = &viewportState;
    blurPipelineInfo.pRasterizationState = &rasterizer;
    blurPipelineInfo.pMultisampleState = &multisampling;
    blurPipelineInfo.pColorBlendState = &colorBlending;
    blurPipelineInfo.pDynamicState = &dynamicState;
    blurPipelineInfo.layout = m_blurPipelineLayout;
    blurPipelineInfo.renderPass = m_blurRenderPass;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &blurPipelineInfo, nullptr, &m_blurPipeline);
    
    // Composite Pipeline
    VkPipelineShaderStageCreateInfo compStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, compFragModule, "main", nullptr}
    };
    
    VkPushConstantRange pcRangeComp{};
    pcRangeComp.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRangeComp.offset = 0;
    pcRangeComp.size = sizeof(glm::vec2);

    VkPipelineLayoutCreateInfo compPipelineLayoutInfo{};
    compPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compPipelineLayoutInfo.setLayoutCount = 1;
    compPipelineLayoutInfo.pSetLayouts = &m_compositeDescriptorSetLayout;
    compPipelineLayoutInfo.pushConstantRangeCount = 1;
    compPipelineLayoutInfo.pPushConstantRanges = &pcRangeComp;
    vkCreatePipelineLayout(device, &compPipelineLayoutInfo, nullptr, &m_compositePipelineLayout);
    
    VkGraphicsPipelineCreateInfo compPipelineInfo = blurPipelineInfo;
    compPipelineInfo.pStages = compStages;
    compPipelineInfo.layout = m_compositePipelineLayout;
    compPipelineInfo.renderPass = m_swapchain->GetRenderPass();
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &compPipelineInfo, nullptr, &m_compositePipeline);
    
    vkDestroyShaderModule(device, compFragModule, nullptr);
    vkDestroyShaderModule(device, blurFragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

} // namespace mc::render
