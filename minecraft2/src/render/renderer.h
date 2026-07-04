#pragma once
#include "vulkan_context.h"
#include "../core/camera.h"
#include "../core/metrics.h"
#include "debug_ui.h"
#include "vertex.h"
#include <vector>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <mutex>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include "../world/chunk_manager.h"

namespace mc::render {

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

class Renderer {
public:
    Renderer(VulkanContext& context, uint32_t width, uint32_t height);
    ~Renderer();

    bool DrawFrame(const mc::core::Camera& camera, float time, mc::core::EngineMetrics& metrics);
    
    void UploadMeshAsync(const mc::world::ChunkMesh& mesh);
    void UnloadChunk(int cx, int cz);
    void ProcessPendingChunks();
    
    bool HandleClick(double x, double y, mc::core::EngineMetrics& metrics);
    void HandleDrag(double x, double y, mc::core::EngineMetrics& metrics);
    void WaitIdle();
    
    void RecreateSwapchain(int width, int height);

private:
    VulkanContext& m_context;
    uint32_t m_width, m_height;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    std::vector<VkImageView> m_swapchainImageViews;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_swapchainFramebuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence m_inFlightFence = VK_NULL_HANDLE;

    struct ChunkRenderData {
        int x, y, z;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
        int minY = 0;
        int maxY = 256;
    };
    std::unordered_map<glm::ivec3, ChunkRenderData> m_chunkRenderData;
    
    std::vector<ChunkRenderData> m_readyRenderData;
    std::mutex m_readyRenderDataMutex;
    
    std::vector<glm::ivec3> m_deletionQueue;
    std::mutex m_deletionQueueMutex;
    
    std::mutex m_commandPoolMutex;
    std::mutex m_graphicsQueueMutex;

    VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_uniformBufferMemory = VK_NULL_HANDLE;
    void* m_uniformBufferMapped = nullptr;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Initialization helpers
    void CreateSwapchain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateDepthResources();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat FindDepthFormat();
    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();

    // Buffer helpers
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    
    void CreateUniformBuffers();
    void CreateDescriptorPool();
    void CreateDescriptorSets();

    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const mc::core::Camera& camera, float time, mc::core::EngineMetrics& metrics);
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);

    std::unique_ptr<DebugUI> m_debugUI;
};

} // namespace mc::render
