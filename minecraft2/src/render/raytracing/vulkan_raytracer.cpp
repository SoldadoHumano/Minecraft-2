#include "vulkan_raytracer.h"
#include "render/vulkan_buffer.h"
#include "render/vulkan_pipeline.h"
#include "render/shader_compiler.h"
#include "logzilla/logzilla.h"
#include <stdexcept>
#include <cstring>
#include "world/block_registry.h"

namespace mc::render {

static const char* RAYTRACE_COMPUTE_GLSL = R"(
#version 450

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout (binding = 0, rgba8) uniform writeonly image2D resultImage;

layout (binding = 1, std140) uniform UBO {
    mat4 invView;
    mat4 invProj;
    vec4 cameraPos; // .w is gridWidth
    vec4 sunDir;    // .w is gridHeight
    ivec4 gridData; // x = gridDepth, y = gridOffsetX, z = gridOffsetZ
} ubo;

layout (binding = 2, std430) readonly buffer VoxelGrid {
    uint grid[];
};

layout (binding = 3) uniform sampler2DArray blockTextures;

layout (binding = 4, std140) uniform BlockLayers {
    ivec4 layers[256]; // x = top, y = side, z = bottom
} blockLayers;

uint GetBlock(int x, int y, int z) {
    int w = int(ubo.cameraPos.w);
    int h = int(ubo.sunDir.w);
    int d = ubo.gridData.x;
    if (x < 0 || x >= w || y < 0 || y >= h || z < 0 || z >= d) return 0;
    int index = x + w * (y + h * z);
    uint word = grid[index / 4];
    uint shift = (index % 4) * 8;
    return (word >> shift) & 0xFFu;
}

bool TraceRayDDA(vec3 ro, vec3 rd, float maxDist, out uint outBlockId, out vec3 outHitPos, out vec3 outNormal) {
    vec3 localRo = ro - vec3(float(ubo.gridData.y) * 16.0f, 0.0f, float(ubo.gridData.z) * 16.0f);
    
    int mapX = int(floor(localRo.x));
    int mapY = int(floor(localRo.y));
    int mapZ = int(floor(localRo.z));

    float deltaDistX = (rd.x == 0.0f) ? 1e30f : abs(1.0f / rd.x);
    float deltaDistY = (rd.y == 0.0f) ? 1e30f : abs(1.0f / rd.y);
    float deltaDistZ = (rd.z == 0.0f) ? 1e30f : abs(1.0f / rd.z);

    float sideDistX = (rd.x < 0.0f) ? (localRo.x - mapX) * deltaDistX : (mapX + 1.0f - localRo.x) * deltaDistX;
    float sideDistY = (rd.y < 0.0f) ? (localRo.y - mapY) * deltaDistY : (mapY + 1.0f - localRo.y) * deltaDistY;
    float sideDistZ = (rd.z < 0.0f) ? (localRo.z - mapZ) * deltaDistZ : (mapZ + 1.0f - localRo.z) * deltaDistZ;

    int stepX = (rd.x < 0.0f) ? -1 : 1;
    int stepY = (rd.y < 0.0f) ? -1 : 1;
    int stepZ = (rd.z < 0.0f) ? -1 : 1;

    int side = 0; 
    float dist = 0.0f;
    while (dist < maxDist) {
        if (sideDistX < sideDistY) {
            if (sideDistX < sideDistZ) {
                dist = sideDistX;
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                dist = sideDistZ;
                sideDistZ += deltaDistZ;
                mapZ += stepZ;
                side = 2;
            }
        } else {
            if (sideDistY < sideDistZ) {
                dist = sideDistY;
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            } else {
                dist = sideDistZ;
                sideDistZ += deltaDistZ;
                mapZ += stepZ;
                side = 2;
            }
        }

        uint block = GetBlock(mapX, mapY, mapZ);
        if (block > 0) {
            outBlockId = block;
            outHitPos = ro + rd * dist;
            if (side == 0) outNormal = vec3(-stepX, 0, 0);
            else if (side == 1) outNormal = vec3(0, -stepY, 0);
            else outNormal = vec3(0, 0, -stepZ);
            return true;
        }
    }
    return false;
}

vec2 GetUV(vec3 hitPos, vec3 normal) {
    vec3 localPos = fract(hitPos);
    if (abs(normal.y) > 0.5) return localPos.xz;
    if (abs(normal.x) > 0.5) return localPos.zy;
    return localPos.xy;
}

int GetTextureLayer(uint blockId, vec3 normal) {
    ivec4 layers = blockLayers.layers[blockId];
    if (normal.y > 0.5) return layers.x; // Top
    if (normal.y < -0.5) return layers.z; // Bottom
    return layers.y; // Side
}

void main() {
    ivec2 dim = imageSize(resultImage);
    ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
    if (coords.x >= dim.x || coords.y >= dim.y) return;

    vec2 uv = vec2(coords) / vec2(dim);
    vec2 ndc = uv * 2.0 - 1.0;
    
    vec4 target = ubo.invProj * vec4(ndc.x, ndc.y, 1.0, 1.0);
    vec3 rayDir = (ubo.invView * vec4(normalize(target.xyz / target.w), 0.0)).xyz;
    vec3 rayOrigin = ubo.cameraPos.xyz;

    uint hitBlockId;
    vec3 hitPos;
    vec3 hitNormal;
    vec3 finalColor = vec3(0.5, 0.7, 1.0); // Sky color

    if (TraceRayDDA(rayOrigin, rayDir, 256.0, hitBlockId, hitPos, hitNormal)) {
        vec2 hitUV = GetUV(hitPos, hitNormal);
        int layer = GetTextureLayer(hitBlockId, hitNormal);
        vec3 albedo = texture(blockTextures, vec3(hitUV, float(layer))).rgb;
        
        vec3 shadowRayOrigin = hitPos + hitNormal * 0.001; // Epsilon to prevent acne
        uint shadowHitBlockId;
        vec3 shadowHitPos;
        vec3 shadowHitNormal;

        float ambient = 0.3;
        float diffuse = max(dot(hitNormal, ubo.sunDir.xyz), 0.0);
        
        bool inShadow = TraceRayDDA(shadowRayOrigin, ubo.sunDir.xyz, 150.0, shadowHitBlockId, shadowHitPos, shadowHitNormal);
        if (inShadow) {
            diffuse = 0.0;
        }

        float lighting = ambient + diffuse * 0.7;
        finalColor = albedo * lighting;
    } else {
        float sunDot = dot(rayDir, ubo.sunDir.xyz);
        if (sunDot > 0.985) {
            finalColor = vec3(2.0, 2.0, 1.5);
        }
    }

    imageStore(resultImage, coords, vec4(finalColor, 1.0));
}
)";

VulkanRaytracer::VulkanRaytracer(VulkanContext& context, VkCommandPool commandPool) 
    : m_context(context), m_commandPool(commandPool) {
}

VkCommandBuffer VulkanRaytracer::BeginSingleTimeCommands() {
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

void VulkanRaytracer::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_context.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_context.GetGraphicsQueue());

    vkFreeCommandBuffers(m_context.GetDevice(), m_commandPool, 1, &commandBuffer);
}

VulkanRaytracer::~VulkanRaytracer() {
    Shutdown();
}

void VulkanRaytracer::Init(int width, int height, VkImageView textureArrayView, VkSampler textureSampler) {
    LOGZILLA_INFO("VulkanRaytracer::Init Start");
    m_blockLayersBufferSize = sizeof(glm::ivec4) * 256;
    LOGZILLA_INFO("Creating block layers buffer...");
    VulkanBuffer::CreateBuffer(m_context,
        m_blockLayersBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_blockLayersBuffer, m_blockLayersMemory);
    
    LOGZILLA_INFO("Updating block layers...");
    UpdateBlockLayers();

    LOGZILLA_INFO("Creating descriptor sets...");
    CreateDescriptorSets();
    
    LOGZILLA_INFO("Creating compute pipeline...");
    CreateComputePipeline();
    
    m_textureArrayView = textureArrayView;
    m_textureSampler = textureSampler;
    LOGZILLA_INFO("Resizing...");
    Resize(width, height);
    LOGZILLA_INFO("VulkanRaytracer::Init End");
}

void VulkanRaytracer::UpdateBlockLayers() {
    void* data;
    vkMapMemory(m_context.GetDevice(), m_blockLayersMemory, 0, m_blockLayersBufferSize, 0, &data);
    std::vector<glm::ivec4> layers(256);
    for(int i = 0; i < 256; ++i) {
        const auto& info = mc::world::BlockRegistry::Get().GetTextureInfo(static_cast<mc::world::BlockType>(i));
        layers[i] = glm::ivec4(info.topLayer, info.sideLayer, info.bottomLayer, 0);
    }
    memcpy(data, layers.data(), m_blockLayersBufferSize);
    vkUnmapMemory(m_context.GetDevice(), m_blockLayersMemory);
}

void VulkanRaytracer::Shutdown() {
    VkDevice device = m_context.GetDevice();
    if (m_outputSampler) vkDestroySampler(device, m_outputSampler, nullptr);
    if (m_outputImageView) vkDestroyImageView(device, m_outputImageView, nullptr);
    if (m_outputImage) vkDestroyImage(device, m_outputImage, nullptr);
    if (m_outputImageMemory) vkFreeMemory(device, m_outputImageMemory, nullptr);

    if (m_voxelGridBuffer) vkDestroyBuffer(device, m_voxelGridBuffer, nullptr);
    if (m_voxelGridMemory) vkFreeMemory(device, m_voxelGridMemory, nullptr);

    if (m_stagingBuffer) vkDestroyBuffer(device, m_stagingBuffer, nullptr);
    if (m_stagingMemory) vkFreeMemory(device, m_stagingMemory, nullptr);

    if (m_blockLayersBuffer) vkDestroyBuffer(device, m_blockLayersBuffer, nullptr);
    if (m_blockLayersMemory) vkFreeMemory(device, m_blockLayersMemory, nullptr);

    if (m_uniformBuffer) {
        m_uniformBuffer = VK_NULL_HANDLE;
        m_uniformBufferMemory = VK_NULL_HANDLE;
    }

    if (m_descriptorPool) {
        vkDestroyDescriptorPool(m_context.GetDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(m_context.GetDevice(), m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(m_context.GetDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_computePipeline) {
        vkDestroyPipeline(m_context.GetDevice(), m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
}

void VulkanRaytracer::CreateDescriptorSets() {
    // Layout
    VkDescriptorSetLayoutBinding imageBinding{};
    imageBinding.binding = 0;
    imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    imageBinding.descriptorCount = 1;
    imageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 1;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding gridBinding{};
    gridBinding.binding = 2;
    gridBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    gridBinding.descriptorCount = 1;
    gridBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding texBinding{};
    texBinding.binding = 3;
    texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount = 1;
    texBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding layersBinding{};
    layersBinding.binding = 4;
    layersBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layersBinding.descriptorCount = 1;
    layersBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {imageBinding, uboBinding, gridBinding, texBinding, layersBinding};
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_context.GetDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute descriptor set layout!");
    }

    // Pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(m_context.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute descriptor pool!");
    }

    // Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(m_context.GetDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate compute descriptor set!");
    }

    // Uniform buffer
    VulkanBuffer::CreateBuffer(m_context, sizeof(UniformData), 
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_uniformBuffer, m_uniformBufferMemory);
    vkMapMemory(m_context.GetDevice(), m_uniformBufferMemory, 0, sizeof(UniformData), 0, &m_uniformMapped);

    // Write UBO to descriptor set
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformData);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 1;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_context.GetDevice(), 1, &descriptorWrite, 0, nullptr);
}

void VulkanRaytracer::CreateComputePipeline() {
    std::vector<uint32_t> spirv = ShaderCompiler::CompileGLSLToSPIRV(RAYTRACE_COMPUTE_GLSL, VK_SHADER_STAGE_COMPUTE_BIT);
    PipelineObjects objs = VulkanPipeline::CreateComputePipeline(m_context, m_descriptorSetLayout, spirv);
    m_pipelineLayout = objs.layout;
    m_computePipeline = objs.pipeline;
}

void VulkanRaytracer::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    
    m_width = width;
    m_height = height;

    if (m_outputImage) {
        vkDestroyImageView(m_context.GetDevice(), m_outputImageView, nullptr);
        vkDestroyImage(m_context.GetDevice(), m_outputImage, nullptr);
        vkFreeMemory(m_context.GetDevice(), m_outputImageMemory, nullptr);
    }
    if (m_outputSampler) {
        vkDestroySampler(m_context.GetDevice(), m_outputSampler, nullptr);
    }

    CreateOutputImage(width, height);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Compute uses general
    imageInfo.imageView = m_outputImageView;
    imageInfo.sampler = m_outputSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    VkDescriptorImageInfo texInfo{};
    texInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texInfo.imageView = m_textureArrayView;
    texInfo.sampler = m_textureSampler;
    
    VkWriteDescriptorSet texWrite{};
    texWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    texWrite.dstSet = m_descriptorSet;
    texWrite.dstBinding = 3;
    texWrite.dstArrayElement = 0;
    texWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texWrite.descriptorCount = 1;
    texWrite.pImageInfo = &texInfo;

    VkDescriptorBufferInfo layersInfo{};
    layersInfo.buffer = m_blockLayersBuffer;
    layersInfo.offset = 0;
    layersInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet layersWrite{};
    layersWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    layersWrite.dstSet = m_descriptorSet;
    layersWrite.dstBinding = 4;
    layersWrite.dstArrayElement = 0;
    layersWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layersWrite.descriptorCount = 1;
    layersWrite.pBufferInfo = &layersInfo;

    VkWriteDescriptorSet writes[] = {descriptorWrite, texWrite, layersWrite};
    vkUpdateDescriptorSets(m_context.GetDevice(), 3, writes, 0, nullptr);
}

void VulkanRaytracer::CreateOutputImage(int width, int height) {
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
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_context.GetDevice(), &imageInfo, nullptr, &m_outputImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute output image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_context.GetDevice(), m_outputImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context.FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_context.GetDevice(), &allocInfo, nullptr, &m_outputImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate compute image memory!");
    }

    vkBindImageMemory(m_context.GetDevice(), m_outputImage, m_outputImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_outputImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_context.GetDevice(), &viewInfo, nullptr, &m_outputImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute image view!");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(m_context.GetDevice(), &samplerInfo, nullptr, &m_outputSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute sampler!");
    }

    // Transition image to general layout for compute shader
    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_outputImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmdBuf);
}

void VulkanRaytracer::UpdateVoxelGrid(const std::vector<uint8_t>& flattenedBlocks, int sizeX, int sizeY, int sizeZ, int chunkOffsetX, int chunkOffsetZ) {
    size_t gridSize = flattenedBlocks.size();
    if (gridSize == 0) return;

    if (gridSize != m_voxelGridBufferSize) {
        if (m_voxelGridBuffer) {
            vkDestroyBuffer(m_context.GetDevice(), m_voxelGridBuffer, nullptr);
            vkFreeMemory(m_context.GetDevice(), m_voxelGridMemory, nullptr);
        }
        if (m_stagingBuffer) {
            vkDestroyBuffer(m_context.GetDevice(), m_stagingBuffer, nullptr);
            vkFreeMemory(m_context.GetDevice(), m_stagingMemory, nullptr);
        }

        VulkanBuffer::CreateBuffer(m_context, gridSize, 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_stagingBuffer, m_stagingMemory);

        VulkanBuffer::CreateBuffer(m_context, gridSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_voxelGridBuffer, m_voxelGridMemory);

        m_voxelGridBufferSize = gridSize;
        
        // Update descriptor set
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_voxelGridBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSet;
        descriptorWrite.dstBinding = 2;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_context.GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    void* data;
    vkMapMemory(m_context.GetDevice(), m_stagingMemory, 0, gridSize, 0, &data);
    memcpy(data, flattenedBlocks.data(), gridSize);
    vkUnmapMemory(m_context.GetDevice(), m_stagingMemory);

    // Copy to device local buffer
    std::mutex dummyMutex;
    VulkanBuffer::CopyBuffer(m_context, m_stagingBuffer, m_voxelGridBuffer, gridSize, 0, 0, dummyMutex);

    // Update uniform data with grid dims
    UniformData* ubo = (UniformData*)m_uniformMapped;
    ubo->cameraPos.w = static_cast<float>(sizeX);
    ubo->sunDir.w = static_cast<float>(sizeY);
    ubo->gridData.x = sizeZ;
    ubo->gridData.y = chunkOffsetX;
    ubo->gridData.z = chunkOffsetZ;
}

void VulkanRaytracer::RenderFrame(VkCommandBuffer cmdBuf, const glm::mat4& invView, const glm::mat4& invProj, const glm::vec3& cameraPos, const glm::vec3& sunDir) {
    if (!m_voxelGridBuffer || !m_outputImage) return;

    UniformData* ubo = (UniformData*)m_uniformMapped;
    ubo->invView = invView;
    ubo->invProj = invProj;
    ubo->cameraPos.x = cameraPos.x;
    ubo->cameraPos.y = cameraPos.y;
    ubo->cameraPos.z = cameraPos.z;
    ubo->sunDir.x = sunDir.x;
    ubo->sunDir.y = sunDir.y;
    ubo->sunDir.z = sunDir.z;

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    
    // Dispatch compute shader
    vkCmdDispatch(cmdBuf, (m_width + 15) / 16, (m_height + 15) / 16, 1);

    // Memory barrier to ensure compute write finishes before fragment shader read
    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.image = m_outputImage;
    imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vkCmdPipelineBarrier(
        cmdBuf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &imageMemoryBarrier);
}

} // namespace mc::render
