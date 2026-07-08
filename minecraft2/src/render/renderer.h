#pragma once
#include "../core/camera.h"
#include "../core/metrics.h"
#include "debug_ui.h"
#include "vertex.h"
#include "vulkan_context.h"
#include "vulkan_swapchain.h"
#include "free_list_allocator.h"
#include "vulkan_pipeline.h"
#include "vulkan_buffer.h"
#include "texture_manager.h"
#include "skybox/skybox_renderer.h"
#include "raytracing/vulkan_raytracer.h"
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#define GLM_ENABLE_EXPERIMENTAL
#include "../world/chunk_manager.h"
#include <glm/gtx/hash.hpp>

namespace mc::render {

struct UniformBufferObject {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};



class Renderer {
public:
  Renderer(VulkanContext &context, uint32_t width, uint32_t height);
  ~Renderer();

  bool DrawFrame(const mc::core::Camera &camera, float time,
                 mc::core::EngineMetrics &metrics, mc::world::ChunkManager &chunkManager);


  void UploadMeshAsync(const mc::world::ChunkMesh &mesh);
  void UnloadChunk(int cx, int cz);
  void ProcessPendingChunks(VkCommandBuffer commandBuffer, uint32_t imageIndex);

  void WaitIdle();

  void RecreateSwapchain(int width, int height);

private:
  VulkanContext &m_context;
  uint32_t m_width, m_height;

  std::unique_ptr<VulkanSwapchain> m_swapchain;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
  VkCommandPool m_commandPool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> m_commandBuffers;

  VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
  VkFence m_inFlightFence = VK_NULL_HANDLE;

  // Offscreen HDR Render Target
  VkImage m_offscreenColorImage = VK_NULL_HANDLE;
  VkDeviceMemory m_offscreenColorMemory = VK_NULL_HANDLE;
  VkImageView m_offscreenColorView = VK_NULL_HANDLE;
  
  VkImage m_offscreenDepthImage = VK_NULL_HANDLE;
  VkDeviceMemory m_offscreenDepthMemory = VK_NULL_HANDLE;
  VkImageView m_offscreenDepthView = VK_NULL_HANDLE;
  
  VkRenderPass m_mainRenderPass = VK_NULL_HANDLE;
  VkFramebuffer m_mainFramebuffer = VK_NULL_HANDLE;
  
  // Blur Pass Render Target (Ping-pong buffer for separable blur)
  VkImage m_blurImage = VK_NULL_HANDLE;
  VkDeviceMemory m_blurMemory = VK_NULL_HANDLE;
  VkImageView m_blurView = VK_NULL_HANDLE;
  
  VkRenderPass m_blurRenderPass = VK_NULL_HANDLE;
  VkFramebuffer m_blurFramebuffer = VK_NULL_HANDLE;
  
  // Post-processing pipelines
  VkPipelineLayout m_blurPipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_blurPipeline = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_blurDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_blurDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_blurDescriptorSet = VK_NULL_HANDLE; // Points to m_offscreenColorView
  
  VkPipelineLayout m_compositePipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_compositePipeline = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_compositeDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_compositeDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_compositeDescriptorSet = VK_NULL_HANDLE; // Points to m_offscreenColorView and m_blurView

  struct ChunkRenderData {
    int x, y, z;
    int minY, maxY;
    FreeListAllocator::Allocation vertexAllocation;
    FreeListAllocator::Allocation indexAllocation;
    uint32_t indexCount = 0;
    uint32_t queryIndex = 0;
    bool isVisible = true;
  };
  std::unordered_map<glm::ivec3, ChunkRenderData> m_chunkRenderData;

  std::vector<ChunkRenderData> m_readyRenderData;
  std::mutex m_readyRenderDataMutex;

  std::vector<glm::ivec3> m_deletionQueue;
  std::mutex m_deletionQueueMutex;

  struct BufferGarbage {
    FreeListAllocator::Allocation vertexAlloc;
    FreeListAllocator::Allocation indexAlloc;
  };
  std::vector<BufferGarbage> m_garbageQueue;

  std::mutex m_commandPoolMutex;
  std::mutex m_graphicsQueueMutex;

  // Global buffers
  VkBuffer m_globalVertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_globalVertexBufferMemory = VK_NULL_HANDLE;
  FreeListAllocator m_vertexAllocator;

  VkBuffer m_globalIndexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_globalIndexBufferMemory = VK_NULL_HANDLE;
  FreeListAllocator m_indexAllocator;

  // Staging buffers for concurrent uploading (One per frame in flight)
  static const int MAX_FRAMES_IN_FLIGHT = 3;
  VkBuffer m_stagingBuffers[MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
  VkDeviceMemory m_stagingBufferMemories[MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
  void* m_stagingBuffersMapped[MAX_FRAMES_IN_FLIGHT] = {nullptr};
  VkDeviceSize m_stagingBufferOffsets[MAX_FRAMES_IN_FLIGHT] = {0};

  std::vector<mc::world::ChunkMesh> m_pendingMeshUploads;
  std::mutex m_pendingUploadsMutex;

  VkCommandPool m_uploadCommandPool = VK_NULL_HANDLE;

  VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_uniformBufferMemory = VK_NULL_HANDLE;
  void *m_uniformBufferMapped = nullptr;

  VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

  // Occlusion Culling
  VkQueryPool m_queryPool = VK_NULL_HANDLE;
  std::vector<uint32_t> m_freeQueryIndices;
  std::vector<uint32_t> m_queryResults;
  std::vector<uint32_t> m_issuedQueries;
  bool m_queriesIssued = false;
  VkPipelineLayout m_aabbPipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_aabbPipeline = VK_NULL_HANDLE;
  VkBuffer m_aabbVertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_aabbVertexBufferMemory = VK_NULL_HANDLE;
  VkBuffer m_aabbIndexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_aabbIndexBufferMemory = VK_NULL_HANDLE;

  // Initialization helpers
  void CreateDescriptorSetLayout();
  void CreateCommandPool();
  void CreateCommandBuffers();
  void CreateSyncObjects();
  void CreateGlobalBuffers();
  void CreateQueryPool();
  void CreateAABBBuffer();

  void CreateUniformBuffers();
  void CreateDescriptorPool();
  void CreateDescriptorSets();

  void CreateOffscreenResources();
  void CreatePostProcessPipelines();

  void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                           const mc::core::Camera &camera, float time,
                           mc::core::EngineMetrics &metrics, mc::world::ChunkManager &chunkManager);


  std::unique_ptr<DebugUI> m_debugUI;
  std::unique_ptr<TextureManager> m_textureManager;
  std::unique_ptr<SkyboxRenderer> m_skybox;
  std::unique_ptr<VulkanRaytracer> m_vulkanRaytracer;
  VkDescriptorSet m_raytraceDescriptorSet = VK_NULL_HANDLE;
};


} // namespace mc::render
