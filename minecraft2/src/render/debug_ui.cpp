#include "debug_ui.h"
#include "shader_compiler.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#define NANOSVG_IMPLEMENTATION
#include "../../third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../../third_party/nanosvgrast.h"

#include <glm/glm.hpp>

namespace mc::render {

const std::string uiVertShaderCode = R"(
#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    vec2 scale;
    vec2 translate;
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
    gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
    fragUV = inUV;
    fragColor = inColor;
}
)";

const std::string uiFragShaderCode = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(binding = 0) uniform sampler2D texSampler;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 tex = texture(texSampler, fragUV);
    // The texture has alpha in all channels or actual colors.
    outColor = fragColor * tex;
}
)";

DebugUI::DebugUI(VulkanContext& context, VkRenderPass renderPass, uint32_t width, uint32_t height)
    : m_context(context), m_width(width), m_height(height) {
    BuildAtlas();
    CreatePipeline(renderPass);
    CreateBuffers();
}

DebugUI::~DebugUI() {
    VkDevice device = m_context.GetDevice();
    vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    vkFreeMemory(device, m_vertexBufferMemory, nullptr);
    
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    
    vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    
    vkDestroySampler(device, m_sampler, nullptr);
    vkDestroyImageView(device, m_atlasView, nullptr);
    vkDestroyImage(device, m_atlasImage, nullptr);
    vkFreeMemory(device, m_atlasMemory, nullptr);
}

void DebugUI::Resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
}

void DebugUI::BuildAtlas() {
    const uint32_t ATLAS_WIDTH = 1024;
    const uint32_t ATLAS_HEIGHT = 1024;
    std::vector<unsigned char> atlasPixels(ATLAS_WIDTH * ATLAS_HEIGHT * 4, 0);

    // 1. Bake Font (stb_truetype)
    std::ifstream fontFile("C:/Windows/Fonts/consola.ttf", std::ios::binary | std::ios::ate);
    if (!fontFile.is_open()) fontFile.open("C:/Windows/Fonts/arial.ttf", std::ios::binary | std::ios::ate);
    
    if (fontFile.is_open()) {
        std::streamsize size = fontFile.tellg();
        fontFile.seekg(0, std::ios::beg);
        std::vector<char> fontBuffer(size);
        if (fontFile.read(fontBuffer.data(), size)) {
            std::vector<unsigned char> tempBitmap(ATLAS_WIDTH * ATLAS_HEIGHT);
            m_charData.resize(96);
            stbtt_BakeFontBitmap((const unsigned char*)fontBuffer.data(), 0, 24.0f, tempBitmap.data(), ATLAS_WIDTH, ATLAS_HEIGHT, 32, 96, (stbtt_bakedchar*)m_charData.data());
            
            for (uint32_t i = 0; i < ATLAS_WIDTH * ATLAS_HEIGHT; ++i) {
                atlasPixels[i * 4 + 0] = 255;
                atlasPixels[i * 4 + 1] = 255;
                atlasPixels[i * 4 + 2] = 255;
                atlasPixels[i * 4 + 3] = tempBitmap[i];
            }
        }
    } else {
        std::cerr << "Warning: Could not load system font for Debug UI!\n";
        m_charData.resize(96, {});
    }

    // 2. Parse SVG UI Elements (nanosvg)
    const char* uiSVG = R"(
        <svg width="256" height="64" xmlns="http://www.w3.org/2000/svg">
            <!-- Crosshair (0..64) -->
            <line x1="32" y1="16" x2="32" y2="48" stroke="white" stroke-width="2"/>
            <line x1="16" y1="32" x2="48" y2="32" stroke="white" stroke-width="2"/>
            
            <!-- Menu Bg (64..128) -->
            <rect x="64" y="0" width="64" height="64" fill="black" fill-opacity="0.8"/>
            
            <!-- Plus Btn (128..192) -->
            <rect x="132" y="4" width="56" height="56" fill="gray"/>
            <path d="M144 32 H176 M160 16 V48" stroke="white" stroke-width="4"/>
            
            <!-- Minus Btn (192..256) -->
            <rect x="196" y="4" width="56" height="56" fill="gray"/>
            <path d="M208 32 H240" stroke="white" stroke-width="4"/>
        </svg>
    )";
    char* svgStr = strdup(uiSVG);
    NSVGimage* svgImage = nsvgParse(svgStr, "px", 96.0f);
    if (svgImage) {
        NSVGrasterizer* rast = nsvgCreateRasterizer();
        std::vector<unsigned char> svgPixels(256 * 64 * 4, 0);
        nsvgRasterize(rast, svgImage, 0, 0, 1.0f, svgPixels.data(), 256, 64, 256 * 4);
        
        // Pack into the bottom right of the atlas
        uint32_t dstX = ATLAS_WIDTH - 256;
        uint32_t dstY = ATLAS_HEIGHT - 64;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 256; ++x) {
                uint32_t srcIdx = (y * 256 + x) * 4;
                uint32_t dstIdx = ((dstY + y) * ATLAS_WIDTH + (dstX + x)) * 4;
                atlasPixels[dstIdx + 0] = svgPixels[srcIdx + 0];
                atlasPixels[dstIdx + 1] = svgPixels[srcIdx + 1];
                atlasPixels[dstIdx + 2] = svgPixels[srcIdx + 2];
                atlasPixels[dstIdx + 3] = svgPixels[srcIdx + 3];
            }
        }
        
        auto setUV = [&](float* uv, int ox) {
            uv[0] = (float)(dstX + ox) / ATLAS_WIDTH;
            uv[1] = (float)dstY / ATLAS_HEIGHT;
            uv[2] = (float)(dstX + ox + 64) / ATLAS_WIDTH;
            uv[3] = (float)(dstY + 64) / ATLAS_HEIGHT;
        };
        
        setUV(m_crosshairUV, 0);
        setUV(m_menuBgUV, 64);
        setUV(m_btnPlusUV, 128);
        setUV(m_btnMinusUV, 192);
        
        nsvgDeleteRasterizer(rast);
        nsvgDelete(svgImage);
    }
    free(svgStr);

    UploadTexture(atlasPixels.data(), ATLAS_WIDTH, ATLAS_HEIGHT);
}

// Memory & Buffer helper
void DebugUI::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_context.GetDevice(), &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.GetDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context.FindMemoryType(memRequirements.memoryTypeBits, properties);

    vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr, &bufferMemory);
    vkBindBufferMemory(m_context.GetDevice(), buffer, bufferMemory, 0);
}

// Quick texture upload
void DebugUI::UploadTexture(unsigned char* pixels, uint32_t width, uint32_t height) {
    VkDeviceSize imageSize = width * height * 4;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_context.GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_context.GetDevice(), stagingBufferMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(m_context.GetDevice(), &imageInfo, nullptr, &m_atlasImage);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.GetDevice(), m_atlasImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context.FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr, &m_atlasMemory);
    vkBindImageMemory(m_context.GetDevice(), m_atlasImage, m_atlasMemory, 0);

    // Need a command buffer to transition layout and copy
    QueueFamilyIndices queueFamilyIndices = m_context.FindQueueFamilies(m_context.GetPhysicalDevice());
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    
    VkCommandPool commandPool;
    vkCreateCommandPool(m_context.GetDevice(), &poolInfo, nullptr, &commandPool);
    
    VkCommandBufferAllocateInfo allocCmdInfo{};
    allocCmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCmdInfo.commandPool = commandPool;
    allocCmdInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_context.GetDevice(), &allocCmdInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Transition Undefined -> TransferDst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_atlasImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition TransferDst -> ShaderReadOnly
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(m_context.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_context.GetGraphicsQueue());

    vkFreeCommandBuffers(m_context.GetDevice(), commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(m_context.GetDevice(), commandPool, nullptr);

    vkDestroyBuffer(m_context.GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_context.GetDevice(), stagingBufferMemory, nullptr);

    // Create ImageView and Sampler
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_atlasImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(m_context.GetDevice(), &viewInfo, nullptr, &m_atlasView);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(m_context.GetDevice(), &samplerInfo, nullptr, &m_sampler);
}

void DebugUI::CreatePipeline(VkRenderPass renderPass) {
    auto vertSpv = ShaderCompiler::CompileGLSLToSPIRV(uiVertShaderCode, VK_SHADER_STAGE_VERTEX_BIT);
    auto fragSpv = ShaderCompiler::CompileGLSLToSPIRV(uiFragShaderCode, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = vertSpv.size() * sizeof(uint32_t);
    createInfo.pCode = vertSpv.data();
    VkShaderModule vertShaderModule;
    vkCreateShaderModule(m_context.GetDevice(), &createInfo, nullptr, &vertShaderModule);

    createInfo.codeSize = fragSpv.size() * sizeof(uint32_t);
    createInfo.pCode = fragSpv.data();
    VkShaderModule fragShaderModule;
    vkCreateShaderModule(m_context.GetDevice(), &createInfo, nullptr, &fragShaderModule);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr }
    };

    VkVertexInputBindingDescription bindingDescription{0, sizeof(UI_Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributeDescriptions[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UI_Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UI_Vertex, uv)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI_Vertex, color)}
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    VkRect2D scissor{};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

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
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Descriptor Set Layout
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;
    vkCreateDescriptorSetLayout(m_context.GetDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout);

    // Push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(m_context.GetDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout);

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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_context.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create UI pipeline!");
    }

    vkDestroyShaderModule(m_context.GetDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_context.GetDevice(), vertShaderModule, nullptr);

    // Descriptor Pool & Sets
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(m_context.GetDevice(), &poolInfo, nullptr, &m_descriptorPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    vkAllocateDescriptorSets(m_context.GetDevice(), &allocInfo, &m_descriptorSet);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_atlasView;
    imageInfo.sampler = m_sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_context.GetDevice(), 1, &descriptorWrite, 0, nullptr);
}

void DebugUI::CreateBuffers() {
    VkDeviceSize bufferSize = sizeof(UI_Vertex) * m_maxVertices;
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_vertexBuffer, m_vertexBufferMemory);
    vkMapMemory(m_context.GetDevice(), m_vertexBufferMemory, 0, bufferSize, 0, &m_vertexBufferMapped);
}

void DebugUI::Draw(VkCommandBuffer cmd, mc::core::EngineMetrics& metrics) {
    if (m_charData.empty()) return;

    std::vector<UI_Vertex> vertices;
    auto addText = [&](float x, float y, const std::string& text) {
        float cx = x;
        float cy = y;
        for (char c : text) {
            if (c >= 32 && c < 128) {
                stbtt_aligned_quad q;
                // Cast charData data pointer since we used a custom struct to avoid exposing stbtt header in our header
                stbtt_GetBakedQuad((stbtt_bakedchar*)m_charData.data(), 1024, 1024, c - 32, &cx, &cy, &q, 1);
                
                vertices.push_back({{q.x0, q.y0}, {q.s0, q.t0}, {1.0f, 1.0f, 1.0f, 1.0f}});
                vertices.push_back({{q.x1, q.y0}, {q.s1, q.t0}, {1.0f, 1.0f, 1.0f, 1.0f}});
                vertices.push_back({{q.x1, q.y1}, {q.s1, q.t1}, {1.0f, 1.0f, 1.0f, 1.0f}});
                
                vertices.push_back({{q.x0, q.y0}, {q.s0, q.t0}, {1.0f, 1.0f, 1.0f, 1.0f}});
                vertices.push_back({{q.x1, q.y1}, {q.s1, q.t1}, {1.0f, 1.0f, 1.0f, 1.0f}});
                vertices.push_back({{q.x0, q.y1}, {q.s0, q.t1}, {1.0f, 1.0f, 1.0f, 1.0f}});
            }
        }
    };

    auto addQuad = [&](float x, float y, float w, float h, float* uv, glm::vec4 col = {1,1,1,1}) {
        vertices.push_back({{x, y}, {uv[0], uv[1]}, {col.r, col.g, col.b, col.a}});
        vertices.push_back({{x+w, y}, {uv[2], uv[1]}, {col.r, col.g, col.b, col.a}});
        vertices.push_back({{x+w, y+h}, {uv[2], uv[3]}, {col.r, col.g, col.b, col.a}});
        
        vertices.push_back({{x, y}, {uv[0], uv[1]}, {col.r, col.g, col.b, col.a}});
        vertices.push_back({{x+w, y+h}, {uv[2], uv[3]}, {col.r, col.g, col.b, col.a}});
        vertices.push_back({{x, y+h}, {uv[0], uv[3]}, {col.r, col.g, col.b, col.a}});
    };

    if (metrics.isPaused) {
        // Draw Pause Menu
        float cx = m_width / 2.0f;
        float cy = m_height / 2.0f;
        
        // Menu Background
        addQuad(cx - 150, cy - 100, 300, 200, m_menuBgUV);
        
        addText(cx - 100, cy - 50, "Game Paused");
        addText(cx - 100, cy, "View Distance: " + std::to_string(metrics.viewDistance));
        
        // Buttons and Slider
        // Track
        addQuad(cx - 100, cy + 30, 200, 20, m_btnPlusUV, glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)); 
        
        // Handle
        float normalized = (float)(metrics.viewDistance - 2) / 126.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        float hx = (cx - 100) + normalized * 180.0f;
        addQuad(hx, cy + 30, 20, 20, m_btnPlusUV);
    } else {
        // Draw Crosshair only when playing
        addQuad(m_width / 2.0f - 32.0f, m_height / 2.0f - 32.0f, 64, 64, m_crosshairUV);
    }

    if (metrics.showF3) {
        addText(10.0f, 30.0f, "Minecraft Clone Engine (Raw Vulkan)");
        addText(10.0f, 60.0f, "FPS: " + std::to_string((int)metrics.fps));
        addText(10.0f, 90.0f, "Frame Time: " + std::to_string(metrics.frameTimeMs) + " ms");
        addText(10.0f, 120.0f, "TPS: " + std::to_string(metrics.tps));
        addText(10.0f, 150.0f, "Vertices Rendered: " + std::to_string(metrics.verticesRendered));
        addText(10.0f, 180.0f, "Chunks Loaded: " + std::to_string(metrics.chunksLoaded));
        addText(10.0f, 210.0f, "Chunks Rendered: " + std::to_string(metrics.chunksRendered));
    }

    if (vertices.empty()) return;

    size_t copySize = vertices.size() * sizeof(UI_Vertex);
    if (copySize > m_maxVertices * sizeof(UI_Vertex)) return; // Prevent overflow
    memcpy(m_vertexBufferMapped, vertices.data(), copySize);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_width;
    viewport.height = (float)m_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {m_width, m_height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    float pushConstants[4] = { 2.0f / m_width, 2.0f / m_height, -1.0f, -1.0f };
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4, pushConstants);

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

bool DebugUI::HandleClick(double mouseX, double mouseY, mc::core::EngineMetrics& metrics) {
    if (!metrics.isPaused) return false;
    
    float cx = m_width / 2.0f;
    float cy = m_height / 2.0f;
    
    // Slider logic
    if (mouseX >= cx - 100 && mouseX <= cx + 100 && mouseY >= cy + 30 && mouseY <= cy + 50) {
        float normalized = (mouseX - (cx - 100)) / 200.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        metrics.viewDistance = 2 + (int)(normalized * 126.0f);
        return true;
    }
    
    return false;
}

void DebugUI::HandleDrag(double mouseX, double mouseY, mc::core::EngineMetrics& metrics) {
    if (!metrics.isPaused) return;
    
    float cx = m_width / 2.0f;
    float cy = m_height / 2.0f;
    
    // Extrapolate slider bounds slightly for easier dragging
    if (mouseX >= cx - 150 && mouseX <= cx + 150 && mouseY >= cy + 10 && mouseY <= cy + 70) {
        float normalized = (mouseX - (cx - 100)) / 200.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        metrics.viewDistance = 2 + (int)(normalized * 126.0f);
    }
}

} // namespace mc::render
