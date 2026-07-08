#include "skybox_renderer.h"
#include "../vulkan_buffer.h"
#include "../shader_compiler.h"
#include "../../logzilla/logzilla.h"
#include <vector>
#include <stdexcept>
#include <cstring>
#include <iostream>

extern "C" void generate_sun_asm(uint32_t* texture_buffer);

namespace mc::render {

static const std::string skyboxVert = R"(
#version 450
layout(push_constant) uniform PushConstants {
    mat4 inverseViewProj;
} pc;

layout(location = 0) out vec3 outRayDir;

void main() {
    // Generate fullscreen quad
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 clipPos = uv * 2.0 - 1.0;
    gl_Position = vec4(clipPos, 0.999, 1.0); // Z near far plane
    
    vec4 target = pc.inverseViewProj * vec4(clipPos.x, clipPos.y, 1.0, 1.0);
    outRayDir = target.xyz / target.w;
}
)";

static const std::string skyboxFrag = R"(
#version 450
layout(location = 0) in vec3 inRayDir;
layout(binding = 0) uniform sampler2D sunTexture;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 dir = normalize(inRayDir);
    // Sky blue (LDR)
    vec3 color = vec3(0.5, 0.7, 1.0);
    
    // Sun position: somewhere in the sky
    vec3 sunDir = normalize(vec3(0.5, 0.8, -0.5));
    
    // Dot product to check if we are looking at the sun
    float d = dot(dir, sunDir);
    if (d > 0.985) { // Size of the sun
        vec3 right = normalize(cross(vec3(0, 1, 0), sunDir));
        vec3 up = cross(sunDir, right);
        
        // Scale determines how big it appears (adjust according to threshold)
        float u = dot(dir, right) * 10.0 + 0.5;
        float v = dot(dir, up) * 10.0 + 0.5;
        
        if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
            vec4 sunColor = texture(sunTexture, vec2(u, v));
            // Multiply the sun color by a large HDR multiplier for bloom!
            color = mix(color, sunColor.rgb * 15.0, sunColor.a);
        }
    }
    
    outColor = vec4(color, 1.0);
}
)";

SkyboxRenderer::SkyboxRenderer(VulkanContext& context, VkRenderPass renderPass)
    : m_context(context) {
    GenerateTexture();
    CreatePipeline(renderPass);
}

SkyboxRenderer::~SkyboxRenderer() {
    VkDevice device = m_context.GetDevice();
    
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    
    vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    
    vkDestroySampler(device, m_sunSampler, nullptr);
    vkDestroyImageView(device, m_sunImageView, nullptr);
    vkDestroyImage(device, m_sunImage, nullptr);
    vkFreeMemory(device, m_sunImageMemory, nullptr);
}

void SkyboxRenderer::Draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    
    // Compute inverse view projection matrix for unprojecting ray directions
    // Remove translation from view matrix so the skybox doesn't move when camera translates
    glm::mat4 skyView = view;
    skyView[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::mat4 inverseViewProj = glm::inverse(proj * skyView);
    
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &inverseViewProj);
    
    vkCmdDraw(cmd, 3, 1, 0, 0); // 3 vertices for fullscreen triangle
}

void SkyboxRenderer::GenerateTexture() {
    LOGZILLA_INFO("Generating ASM Sun Texture...");
    std::vector<uint32_t> pixels(32 * 32);
    generate_sun_asm(pixels.data());
    
    VkDeviceSize imageSize = 32 * 32 * 4;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VulkanBuffer::CreateBuffer(m_context, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuffer, stagingBufferMemory);
                               
    void* data;
    vkMapMemory(m_context.GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_context.GetDevice(), stagingBufferMemory);
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 32;
    imageInfo.extent.height = 32;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_context.GetDevice(), &imageInfo, nullptr, &m_sunImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create sun image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.GetDevice(), m_sunImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_context.GetPhysicalDevice(), &memProperties);
    uint32_t memoryTypeIndex = 0;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            memoryTypeIndex = i;
            break;
        }
    }
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    if (vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr, &m_sunImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate sun image memory!");
    }
    vkBindImageMemory(m_context.GetDevice(), m_sunImage, m_sunImageMemory, 0);
    
    // Copy buffer to image using a temporary command buffer
    // (We reuse VulkanBuffer::CopyBuffer's internal logic or do it manually)
    // To keep it simple, we do a manual one-time submit
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    
    // We need a command pool, we can create a temporary one
    VkCommandPool tempPool;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = m_context.FindQueueFamilies(m_context.GetPhysicalDevice()).graphicsFamily.value();
    vkCreateCommandPool(m_context.GetDevice(), &poolInfo, nullptr, &tempPool);
    
    cmdAllocInfo.commandPool = tempPool;
    cmdAllocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_context.GetDevice(), &cmdAllocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    // Transition layout to TRANSFER_DST
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_sunImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {32, 32, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_sunImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition layout to SHADER_READ
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
    
    vkFreeCommandBuffers(m_context.GetDevice(), tempPool, 1, &commandBuffer);
    vkDestroyCommandPool(m_context.GetDevice(), tempPool, nullptr);
    
    vkDestroyBuffer(m_context.GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_context.GetDevice(), stagingBufferMemory, nullptr);
    
    // Create ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_sunImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(m_context.GetDevice(), &viewInfo, nullptr, &m_sunImageView);
    
    // Create Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST; // Pixel art!
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    vkCreateSampler(m_context.GetDevice(), &samplerInfo, nullptr, &m_sunSampler);
}

void SkyboxRenderer::CreatePipeline(VkRenderPass renderPass) {
    VkDevice device = m_context.GetDevice();
    
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
    
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout);
    
    // Descriptor Pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool);
    
    // Descriptor Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet);
    
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_sunImageView;
    imageInfo.sampler = m_sunSampler;
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
    // Pipeline
    auto vertSpv = ShaderCompiler::CompileGLSLToSPIRV(skyboxVert, VK_SHADER_STAGE_VERTEX_BIT);
    auto fragSpv = ShaderCompiler::CompileGLSLToSPIRV(skyboxFrag, VK_SHADER_STAGE_FRAGMENT_BIT);
    
    VkShaderModuleCreateInfo vertModuleInfo{};
    vertModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertModuleInfo.codeSize = vertSpv.size() * sizeof(uint32_t);
    vertModuleInfo.pCode = vertSpv.data();
    VkShaderModule vertModule;
    vkCreateShaderModule(device, &vertModuleInfo, nullptr, &vertModule);
    
    VkShaderModuleCreateInfo fragModuleInfo{};
    fragModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragModuleInfo.codeSize = fragSpv.size() * sizeof(uint32_t);
    fragModuleInfo.pCode = fragSpv.data();
    VkShaderModule fragModule;
    vkCreateShaderModule(device, &fragModuleInfo, nullptr, &fragModule);
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}
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
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
    
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
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
    
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

} // namespace mc::render
