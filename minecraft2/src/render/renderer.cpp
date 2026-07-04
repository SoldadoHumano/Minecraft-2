#include "renderer.h"
#include "../logzilla/logzilla.h"
#include "shader_compiler.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace mc::render {

Renderer::Renderer(VulkanContext &context, uint32_t width, uint32_t height)
    : m_context(context), m_width(width), m_height(height) {

  ShaderCompiler::Initialize();

  LOGZILLA_INFO("Initializing Swapchain...");
  m_swapchain = std::make_unique<VulkanSwapchain>(m_context, m_width, m_height);

  LOGZILLA_INFO("CreateDescriptorSetLayout...");
  CreateDescriptorSetLayout();

  LOGZILLA_INFO("CreateGraphicsPipeline...");
  PipelineObjects graphicsPipeline = VulkanPipeline::CreateGraphicsPipeline(m_context, m_swapchain->GetRenderPass(), m_descriptorSetLayout, m_swapchain->GetExtent());
  m_pipelineLayout = graphicsPipeline.layout;
  m_graphicsPipeline = graphicsPipeline.pipeline;

  LOGZILLA_INFO("CreateQueryPool...");
  CreateQueryPool();
  LOGZILLA_INFO("CreateAABBPipeline...");
  PipelineObjects aabbPipeline = VulkanPipeline::CreateAABBPipeline(m_context, m_swapchain->GetRenderPass(), m_descriptorSetLayout);
  m_aabbPipelineLayout = aabbPipeline.layout;
  m_aabbPipeline = aabbPipeline.pipeline;

  LOGZILLA_INFO("CreateCommandPool...");
  CreateCommandPool();
  LOGZILLA_INFO("CreateAABBBuffer...");
  CreateAABBBuffer();
  LOGZILLA_INFO("CreateUniformBuffers...");
  CreateUniformBuffers();
  LOGZILLA_INFO("CreateDescriptorPool...");
  CreateDescriptorPool();
  LOGZILLA_INFO("CreateDescriptorSets...");
  CreateDescriptorSets();
  LOGZILLA_INFO("DebugUI init...");
  m_debugUI = std::make_unique<DebugUI>();
  LOGZILLA_INFO("DebugUI created OK.");

  // Init ImGui
  LOGZILLA_INFO("ImGui: IMGUI_CHECKVERSION...");
  IMGUI_CHECKVERSION();
  LOGZILLA_INFO("ImGui: CreateContext...");
  ImGui::CreateContext();
  LOGZILLA_INFO("ImGui: GetIO...");
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  LOGZILLA_INFO("ImGui: StyleColorsDark...");
  ImGui::StyleColorsDark();
  LOGZILLA_INFO("ImGui: Context ready.");

  // Create ImGui Descriptor Pool
  LOGZILLA_INFO("ImGui: Creating descriptor pool...");
  VkDescriptorPoolSize pool_sizes[] =
  {
      { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
      { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
      { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
  };
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
  pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;
  VkResult poolResult = vkCreateDescriptorPool(m_context.GetDevice(), &pool_info, nullptr, &m_imguiDescriptorPool);
  LOGZILLA_INFO("ImGui: Descriptor pool created, result=%d, pool=%p", (int)poolResult, (void*)m_imguiDescriptorPool);

  LOGZILLA_INFO("ImGui: LoadFunctions (instance=%p)...", (void*)m_context.GetInstance());
  ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void* vulkan_instance) {
    return vkGetInstanceProcAddr(reinterpret_cast<VkInstance>(vulkan_instance), function_name);
  }, reinterpret_cast<void*>(m_context.GetInstance()));
  LOGZILLA_INFO("ImGui: LoadFunctions done.");

  LOGZILLA_INFO("ImGui: ImGui_ImplGlfw_InitForVulkan (window=%p)...", (void*)m_context.GetWindow());
  ImGui_ImplGlfw_InitForVulkan(m_context.GetWindow(), true);
  LOGZILLA_INFO("ImGui: ImGui_ImplGlfw_InitForVulkan done.");

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = m_context.GetInstance();
  init_info.PhysicalDevice = m_context.GetPhysicalDevice();
  init_info.Device = m_context.GetDevice();
  init_info.QueueFamily = m_context.FindQueueFamilies(m_context.GetPhysicalDevice()).graphicsFamily.value();
  init_info.Queue = m_context.GetGraphicsQueue();
  init_info.PipelineCache = VK_NULL_HANDLE;
  init_info.DescriptorPool = m_imguiDescriptorPool;
  init_info.RenderPass = m_swapchain->GetRenderPass();
  init_info.Subpass = 0;
  init_info.MinImageCount = 2;
  init_info.ImageCount = static_cast<uint32_t>(m_swapchain->GetImages().size());
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.Allocator = nullptr;
  init_info.CheckVkResultFn = nullptr;
  LOGZILLA_INFO("ImGui: ImGui_ImplVulkan_Init (Instance=%p, PhysDevice=%p, Device=%p, QueueFamily=%d, Queue=%p, RenderPass=%p, MinImg=%d, ImgCount=%d)...",
                (void*)init_info.Instance, (void*)init_info.PhysicalDevice,
                (void*)init_info.Device, init_info.QueueFamily,
                (void*)init_info.Queue, (void*)init_info.RenderPass,
                init_info.MinImageCount, init_info.ImageCount);
  ImGui_ImplVulkan_Init(&init_info);
  LOGZILLA_INFO("ImGui: ImGui_ImplVulkan_Init done.");

  // Upload ImGui fonts
  LOGZILLA_INFO("ImGui: Creating temp command pool for font upload...");
  VkCommandPool tempPool;
  VkCommandPoolCreateInfo tempPoolInfo{};
  tempPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  tempPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  tempPoolInfo.queueFamilyIndex = m_context.FindQueueFamilies(m_context.GetPhysicalDevice()).graphicsFamily.value();
  vkCreateCommandPool(m_context.GetDevice(), &tempPoolInfo, nullptr, &tempPool);
  LOGZILLA_INFO("ImGui: Temp command pool created, pool=%p", (void*)tempPool);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = tempPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd;
  vkAllocateCommandBuffers(m_context.GetDevice(), &allocInfo, &cmd);
  LOGZILLA_INFO("ImGui: Command buffer allocated, cmd=%p", (void*)cmd);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);
  LOGZILLA_INFO("ImGui: Command buffer recording started.");
  
  LOGZILLA_INFO("ImGui: CreateFontsTexture...");
  ImGui_ImplVulkan_CreateFontsTexture();
  LOGZILLA_INFO("ImGui: CreateFontsTexture done.");
  
  vkEndCommandBuffer(cmd);
  LOGZILLA_INFO("ImGui: Command buffer recording ended.");

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  LOGZILLA_INFO("ImGui: Submitting font upload to queue...");
  vkQueueSubmit(m_context.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
  LOGZILLA_INFO("ImGui: vkQueueWaitIdle...");
  vkQueueWaitIdle(m_context.GetGraphicsQueue());
  LOGZILLA_INFO("ImGui: Font upload complete.");
  
  vkFreeCommandBuffers(m_context.GetDevice(), tempPool, 1, &cmd);
  vkDestroyCommandPool(m_context.GetDevice(), tempPool, nullptr);
  ImGui_ImplVulkan_DestroyFontsTexture();
  LOGZILLA_INFO("ImGui: Cleanup temp resources done.");

  LOGZILLA_INFO("CreateCommandBuffers...");
  CreateCommandBuffers();
  LOGZILLA_INFO("CreateSyncObjects...");
  CreateSyncObjects();
  LOGZILLA_INFO("CreateGlobalBuffers...");
  CreateGlobalBuffers();
  LOGZILLA_INFO("Renderer init done!");
}

Renderer::~Renderer() {
  VkDevice device = m_context.GetDevice();
  vkDeviceWaitIdle(device);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (m_imguiDescriptorPool) {
    vkDestroyDescriptorPool(device, m_imguiDescriptorPool, nullptr);
  }

  m_debugUI.reset();

  // AABB Occlusion Culling

  vkDestroySemaphore(device, m_renderFinishedSemaphore, nullptr);
  vkDestroySemaphore(device, m_imageAvailableSemaphore, nullptr);
  vkDestroyFence(device, m_inFlightFence, nullptr);

  vkDestroyCommandPool(device, m_commandPool, nullptr);

  for (auto &pair : m_chunkRenderData) {
    if (pair.second.indexAllocation.valid) m_indexAllocator.Free(pair.second.indexAllocation);
    if (pair.second.vertexAllocation.valid) m_vertexAllocator.Free(pair.second.vertexAllocation);
  }
  
  for (auto &garbage : m_garbageQueue) {
    if (garbage.vertexAlloc.valid) m_vertexAllocator.Free(garbage.vertexAlloc);
    if (garbage.indexAlloc.valid) m_indexAllocator.Free(garbage.indexAlloc);
  }
  m_garbageQueue.clear();
  
  m_chunkRenderData.clear();
  vkDestroyQueryPool(device, m_queryPool, nullptr);

  vkDestroyBuffer(device, m_uniformBuffer, nullptr);
  vkFreeMemory(device, m_uniformBufferMemory, nullptr);

  vkDestroyBuffer(device, m_globalVertexBuffer, nullptr);
  vkFreeMemory(device, m_globalVertexBufferMemory, nullptr);
  vkDestroyBuffer(device, m_globalIndexBuffer, nullptr);
  vkFreeMemory(device, m_globalIndexBufferMemory, nullptr);

  vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

  vkDestroyPipeline(device, m_graphicsPipeline, nullptr);
  vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);

  vkDestroyPipeline(device, m_aabbPipeline, nullptr);
  vkDestroyPipelineLayout(device, m_aabbPipelineLayout, nullptr);

  vkDestroyBuffer(device, m_aabbVertexBuffer, nullptr);
  vkFreeMemory(device, m_aabbVertexBufferMemory, nullptr);
  vkDestroyBuffer(device, m_aabbIndexBuffer, nullptr);
  vkFreeMemory(device, m_aabbIndexBufferMemory, nullptr);

  m_swapchain.reset();

  ShaderCompiler::Finalize();
}

void Renderer::WaitIdle() { vkDeviceWaitIdle(m_context.GetDevice()); }
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

  if (vkCreateDescriptorSetLayout(m_context.GetDevice(), &layoutInfo, nullptr,
                                  &m_descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}
void Renderer::CreateCommandPool() {
  QueueFamilyIndices queueFamilyIndices =
      m_context.FindQueueFamilies(m_context.GetPhysicalDevice());

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

  if (vkCreateCommandPool(m_context.GetDevice(), &poolInfo, nullptr,
                          &m_commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }
}

void Renderer::CreateQueryPool() {
  VkQueryPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  poolInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
  poolInfo.queryCount = 32768; // Increased from 4096

  if (vkCreateQueryPool(m_context.GetDevice(), &poolInfo, nullptr,
                        &m_queryPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create query pool!");
  }

  m_queryResults.resize(32768, 1); // 1 = visible
  m_freeQueryIndices.reserve(32768);
  for (uint32_t i = 1; i < 32768; ++i) { // 0 is reserved for fallback/errors
    m_freeQueryIndices.push_back(i);
  }
}
void Renderer::CreateAABBBuffer() {
  std::vector<glm::vec3> vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0},
                                     {0, 1, 0}, {0, 0, 1}, {1, 0, 1},
                                     {1, 1, 1}, {0, 1, 1}};
  std::vector<uint32_t> indices = {
      0, 1, 2, 2, 3, 0, // front
      1, 5, 6, 6, 2, 1, // right
      5, 4, 7, 7, 6, 5, // back
      4, 0, 3, 3, 7, 4, // left
      3, 2, 6, 6, 7, 3, // top
      4, 5, 1, 1, 0, 4  // bottom
  };

  VkDeviceSize vertexSize = sizeof(vertices[0]) * vertices.size();
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  VulkanBuffer::CreateBuffer(m_context, vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void *data;
  vkMapMemory(m_context.GetDevice(), stagingBufferMemory, 0, vertexSize, 0,
              &data);
  memcpy(data, vertices.data(), (size_t)vertexSize);
  vkUnmapMemory(m_context.GetDevice(), stagingBufferMemory);

  VulkanBuffer::CreateBuffer(m_context, vertexSize,
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_aabbVertexBuffer,
               m_aabbVertexBufferMemory);
  VulkanBuffer::CopyBuffer(m_context, stagingBuffer,  m_aabbVertexBuffer,  vertexSize, 0, 0, m_graphicsQueueMutex);
  vkDestroyBuffer(m_context.GetDevice(), stagingBuffer, nullptr);
  vkFreeMemory(m_context.GetDevice(), stagingBufferMemory, nullptr);

  VkDeviceSize indexSize = sizeof(indices[0]) * indices.size();
  VulkanBuffer::CreateBuffer(m_context, indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  vkMapMemory(m_context.GetDevice(), stagingBufferMemory, 0, indexSize, 0,
              &data);
  memcpy(data, indices.data(), (size_t)indexSize);
  vkUnmapMemory(m_context.GetDevice(), stagingBufferMemory);

  VulkanBuffer::CreateBuffer(m_context, indexSize,
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_aabbIndexBuffer,
               m_aabbIndexBufferMemory);
  VulkanBuffer::CopyBuffer(m_context, stagingBuffer,  m_aabbIndexBuffer,  indexSize, 0, 0, m_graphicsQueueMutex);
  vkDestroyBuffer(m_context.GetDevice(), stagingBuffer, nullptr);
  vkFreeMemory(m_context.GetDevice(), stagingBufferMemory, nullptr);
}
void Renderer::CreateUniformBuffers() {
  VkDeviceSize bufferSize = sizeof(UniformBufferObject);
  VulkanBuffer::CreateBuffer(m_context, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               m_uniformBuffer, m_uniformBufferMemory);
  vkMapMemory(m_context.GetDevice(), m_uniformBufferMemory, 0, bufferSize, 0,
              &m_uniformBufferMapped);
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

  if (vkCreateDescriptorPool(m_context.GetDevice(), &poolInfo, nullptr,
                             &m_descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

void Renderer::CreateDescriptorSets() {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_descriptorSetLayout;

  if (vkAllocateDescriptorSets(m_context.GetDevice(), &allocInfo,
                               &m_descriptorSet) != VK_SUCCESS) {
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

  vkUpdateDescriptorSets(m_context.GetDevice(), 1, &descriptorWrite, 0,
                         nullptr);
}

void Renderer::CreateGlobalBuffers() {
  // 128 MB for vertices (approx 3 million vertices), 64 MB for indices (approx 16 million indices)
  const VkDeviceSize vertexBufferSize = 128 * 1024 * 1024;
  const VkDeviceSize indexBufferSize = 64 * 1024 * 1024;

  VulkanBuffer::CreateBuffer(m_context, vertexBufferSize,
               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_globalVertexBuffer,
               m_globalVertexBufferMemory);

  VulkanBuffer::CreateBuffer(m_context, indexBufferSize,
               VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_globalIndexBuffer,
               m_globalIndexBufferMemory);

  // Initialize allocators with maximum capacities (in vertices / indices)
  uint32_t maxVertices = vertexBufferSize / sizeof(mc::render::Vertex);
  uint32_t maxIndices = indexBufferSize / sizeof(uint32_t);
  
  m_vertexAllocator.Initialize(maxVertices);
  m_indexAllocator.Initialize(maxIndices);
}

void Renderer::CreateCommandBuffers() {
  m_commandBuffers.resize(
      1); // One command buffer for simplicity in this milestone

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(m_context.GetDevice(), &allocInfo,
                               m_commandBuffers.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void Renderer::CreateSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateSemaphore(m_context.GetDevice(), &semaphoreInfo, nullptr,
                        &m_imageAvailableSemaphore) != VK_SUCCESS ||
      vkCreateSemaphore(m_context.GetDevice(), &semaphoreInfo, nullptr,
                        &m_renderFinishedSemaphore) != VK_SUCCESS ||
      vkCreateFence(m_context.GetDevice(), &fenceInfo, nullptr,
                    &m_inFlightFence) != VK_SUCCESS) {
    throw std::runtime_error("failed to create synchronization objects!");
  }
}

void Renderer::RecordCommandBuffer(VkCommandBuffer commandBuffer,
                                   uint32_t imageIndex,
                                   const mc::core::Camera &camera, float time,
                                   mc::core::EngineMetrics &metrics) {
  static int record_count = 0;
  if (record_count < 3) {
    LOGZILLA_INFO("RecordCommandBuffer %d", record_count);
    record_count++;
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = m_swapchain->GetRenderPass();
  renderPassInfo.framebuffer = m_swapchain->GetFramebuffers()[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = m_swapchain->GetExtent();

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {{0.5f, 0.7f, 1.0f, 1.0f}}; // Sky blue clear color
  clearValues[1].depthStencil = {1.0f, 0};

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  std::vector<ChunkRenderData *> chunksToRender;
  chunksToRender.reserve(m_chunkRenderData.size());

  // Frustum Culling Math
  glm::mat4 vp = camera.GetProjectionMatrix() * camera.GetViewMatrix();
  glm::vec4 planes[6];
  planes[0].x = vp[0][3] + vp[0][0];
  planes[0].y = vp[1][3] + vp[1][0];
  planes[0].z = vp[2][3] + vp[2][0];
  planes[0].w = vp[3][3] + vp[3][0]; // Left
  planes[1].x = vp[0][3] - vp[0][0];
  planes[1].y = vp[1][3] - vp[1][0];
  planes[1].z = vp[2][3] - vp[2][0];
  planes[1].w = vp[3][3] - vp[3][0]; // Right
  planes[2].x = vp[0][3] + vp[0][1];
  planes[2].y = vp[1][3] + vp[1][1];
  planes[2].z = vp[2][3] + vp[2][1];
  planes[2].w = vp[3][3] + vp[3][1]; // Bottom
  planes[3].x = vp[0][3] - vp[0][1];
  planes[3].y = vp[1][3] - vp[1][1];
  planes[3].z = vp[2][3] - vp[2][1];
  planes[3].w = vp[3][3] - vp[3][1]; // Top
  planes[4].x = vp[0][3] + vp[0][2];
  planes[4].y = vp[1][3] + vp[1][2];
  planes[4].z = vp[2][3] + vp[2][2];
  planes[4].w = vp[3][3] + vp[3][2]; // Near
  planes[5].x = vp[0][3] - vp[0][2];
  planes[5].y = vp[1][3] - vp[1][2];
  planes[5].z = vp[2][3] - vp[2][2];
  planes[5].w = vp[3][3] - vp[3][2]; // Far

  for (int i = 0; i < 6; ++i) {
    float length =
        std::sqrt(planes[i].x * planes[i].x + planes[i].y * planes[i].y +
                  planes[i].z * planes[i].z);
    planes[i] /= length;
  }

  for (auto &pair : m_chunkRenderData) {
    auto &chunk = pair.second;
    if (chunk.indexCount == 0)
      continue;

    // Frustum AABB test
    glm::vec3 minAABB(chunk.x * 16.0f, chunk.minY, chunk.z * 16.0f);
    glm::vec3 maxAABB(chunk.x * 16.0f + 16.0f, chunk.maxY,
                      chunk.z * 16.0f + 16.0f);

    bool inside = true;
    for (int i = 0; i < 6; ++i) {
      glm::vec3 p = minAABB;
      if (planes[i].x >= 0)
        p.x = maxAABB.x;
      if (planes[i].y >= 0)
        p.y = maxAABB.y;
      if (planes[i].z >= 0)
        p.z = maxAABB.z;

      if (planes[i].x * p.x + planes[i].y * p.y + planes[i].z * p.z +
              planes[i].w <
          0) {
        inside = false;
        break;
      }
    }

    if (inside) {
      chunksToRender.push_back(&chunk);
    }
  }

  m_issuedQueries.clear();
  for (auto *chunk : chunksToRender) {
    vkCmdResetQueryPool(commandBuffer, m_queryPool, chunk->queryIndex, 1);
    m_issuedQueries.push_back(chunk->queryIndex);
  }

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

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

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_graphicsPipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

  // We already frustum culled above!

  // Sort front-to-back for Early-Z and optimal occlusion culling
  glm::vec3 camPos = camera.GetPosition();
  std::sort(chunksToRender.begin(), chunksToRender.end(),
            [&](ChunkRenderData *a, ChunkRenderData *b) {
              glm::vec3 centerA(a->x * 16.0f + 8.0f,
                                a->minY + (a->maxY - a->minY) * 0.5f,
                                a->z * 16.0f + 8.0f);
              glm::vec3 centerB(b->x * 16.0f + 8.0f,
                                b->minY + (b->maxY - b->minY) * 0.5f,
                                b->z * 16.0f + 8.0f);
              return glm::distance(camPos, centerA) <
                     glm::distance(camPos, centerB);
            });

  metrics.verticesRendered = 0;
  metrics.chunksRendered = 0;
  metrics.chunksLoaded = static_cast<uint32_t>(m_chunkRenderData.size());

  // Pass 1: Draw actual geometry (only if visible)
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_graphicsPipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

  if (m_globalVertexBuffer && m_globalIndexBuffer) {
    VkBuffer vertexBuffers[] = {m_globalVertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_globalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    for (auto *chunk : chunksToRender) {
      if (!chunk->isVisible)
        continue;

      metrics.chunksRendered++;
      metrics.verticesRendered += chunk->indexCount; // Actually indices rendered

      glm::vec4 pushData(chunk->x * 16.0f, chunk->y * 16.0f, chunk->z * 16.0f,
                         0.0f);
      vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec4),
                         &pushData);
      
      // Use offsets from sub-allocations
      vkCmdDrawIndexed(commandBuffer, chunk->indexCount, 1, chunk->indexAllocation.start, chunk->vertexAllocation.start, 0);
    }
  }

  // Pass 2: Draw AABBs for Occlusion Queries
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_aabbPipeline);
  VkBuffer aabbVertexBuffers[] = {m_aabbVertexBuffer};
  VkDeviceSize aabbOffsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, aabbVertexBuffers, aabbOffsets);
  vkCmdBindIndexBuffer(commandBuffer, m_aabbIndexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);

  // Bind the descriptor set for UBO since AABB pipeline uses it too
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_aabbPipelineLayout, 0, 1, &m_descriptorSet, 0,
                          nullptr);

  for (auto *chunk : chunksToRender) {
    vkCmdBeginQuery(commandBuffer, m_queryPool, chunk->queryIndex, 0);

    // Expand AABB slightly to prevent self-occlusion Z-fighting which causes flapping
    glm::vec4 minPos(chunk->x * 16.0f - 0.2f, chunk->minY - 0.2f, chunk->z * 16.0f - 0.2f, 0.0f);
    glm::vec4 maxPos(chunk->x * 16.0f + 16.2f, chunk->maxY + 0.2f,
                     chunk->z * 16.0f + 16.2f, 0.0f);

    vkCmdPushConstants(commandBuffer, m_aabbPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec4),
                       &minPos);
    vkCmdPushConstants(commandBuffer, m_aabbPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::vec4),
                       sizeof(glm::vec4), &maxPos);

    vkCmdDrawIndexed(commandBuffer, 36, 1, 0, 0, 0);
    vkCmdEndQuery(commandBuffer, m_queryPool, chunk->queryIndex);
  }

  m_queriesIssued = true;

  m_debugUI->Draw(metrics);
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

  vkCmdEndRenderPass(commandBuffer);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }
}

bool Renderer::DrawFrame(const mc::core::Camera &camera, float time,
                         mc::core::EngineMetrics &metrics) {
  static int frame_count = 0;
  if (frame_count < 3) {
    LOGZILLA_INFO("DrawFrame %d", frame_count);
    frame_count++;
  }

  vkWaitForFences(m_context.GetDevice(), 1, &m_inFlightFence, VK_TRUE,
                  UINT64_MAX);

  // Process pending chunks FIRST, so if a chunk is loaded and then instantly unloaded
  // (or vice-versa), it is properly added to the map before toDelete processes it.
  ProcessPendingChunks();

  // Safe to process pending deletions because GPU is idle for this frame
  std::vector<glm::ivec3> toDelete;
  {
    std::lock_guard<std::mutex> lock(m_deletionQueueMutex);
    toDelete = std::move(m_deletionQueue);
    m_deletionQueue.clear();
  }

  for (const auto &pos : toDelete) {
    auto it = m_chunkRenderData.find(pos);
    if (it != m_chunkRenderData.end()) {
      m_garbageQueue.push_back({it->second.vertexAllocation, it->second.indexAllocation});
      m_freeQueryIndices.push_back(it->second.queryIndex);
      m_chunkRenderData.erase(it);
    }
  }

  // Process some garbage (limit to 128 items per frame to prevent freezing)
  int garbageProcessed = 0;
  while (!m_garbageQueue.empty() && garbageProcessed < 128) {
    auto &garbage = m_garbageQueue.back();
    if (garbage.vertexAlloc.valid) m_vertexAllocator.Free(garbage.vertexAlloc);
    if (garbage.indexAlloc.valid) m_indexAllocator.Free(garbage.indexAlloc);
    m_garbageQueue.pop_back();
    garbageProcessed++;
  }

  // Read back occlusion queries from last frame (since GPU is idle)
  if (m_queriesIssued) {
    // Reset all results to 1 (visible) so chunks outside frustum default to visible
    // and won't flicker when they enter the frustum.
    std::fill(m_queryResults.begin(), m_queryResults.end(), 1);

    for (uint32_t qIdx : m_issuedQueries) {
      uint32_t res = 1; // Default to visible
      vkGetQueryPoolResults(m_context.GetDevice(), m_queryPool, qIdx, 1,
                            sizeof(uint32_t), &res, sizeof(uint32_t),
                            VK_QUERY_RESULT_WAIT_BIT);
      m_queryResults[qIdx] = res;
    }

    for (auto &pair : m_chunkRenderData) {
      pair.second.isVisible = (m_queryResults[pair.second.queryIndex] > 0);
    }
  }

  vkResetFences(m_context.GetDevice(), 1, &m_inFlightFence);

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(m_context.GetDevice(), m_swapchain->GetSwapchain(),
                                          UINT64_MAX, m_imageAvailableSemaphore,
                                          VK_NULL_HANDLE, &imageIndex);

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
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
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
    if (vkQueueSubmit(m_context.GetGraphicsQueue(), 1, &submitInfo,
                      m_inFlightFence) != VK_SUCCESS) {
      throw std::runtime_error("failed to submit draw command buffer!");
    }
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapchains[] = {m_swapchain->GetSwapchain()};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapchains;
  presentInfo.pImageIndices = &imageIndex;

  VkResult resultPresent;
  {
    std::lock_guard<std::mutex> lock(m_graphicsQueueMutex);
    resultPresent =
        vkQueuePresentKHR(m_context.GetPresentQueue(), &presentInfo);
  }

  if (resultPresent == VK_ERROR_OUT_OF_DATE_KHR ||
      resultPresent == VK_SUBOPTIMAL_KHR) {
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

  m_swapchain->Recreate(width, height);
}

void Renderer::UnloadChunk(int cx, int cz) {
  std::lock_guard<std::mutex> lock(m_deletionQueueMutex);
  for (int y = 0; y < 16; ++y) {
    m_deletionQueue.push_back(glm::ivec3(cx, y, cz));
  }
}

void Renderer::ProcessPendingChunks() {
  std::lock_guard<std::mutex> lock(m_readyRenderDataMutex);
  for (auto &data : m_readyRenderData) {
    glm::ivec3 pos(data.x, data.y, data.z);

    // If there was an old chunk at this position, we should clean it up
    auto it = m_chunkRenderData.find(pos);
    if (it != m_chunkRenderData.end()) {
      m_garbageQueue.push_back({it->second.vertexAllocation, it->second.indexAllocation});

      // Preserve query index
      data.queryIndex = it->second.queryIndex;
      data.isVisible = it->second.isVisible;
    } else {
      if (!m_freeQueryIndices.empty()) {
        data.queryIndex = m_freeQueryIndices.back();
        m_freeQueryIndices.pop_back();
      } else {
        data.queryIndex = 0; // Fallback
      }
    }

    m_chunkRenderData[pos] = data;
  }
  m_readyRenderData.clear();
}

void Renderer::UploadMeshAsync(const mc::world::ChunkMesh &mesh) {
  if (mesh.vertices.empty() || mesh.indices.empty())
    return;

  uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
  uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());

  auto vertexAlloc = m_vertexAllocator.Allocate(vertexCount);
  auto indexAlloc = m_indexAllocator.Allocate(indexCount);

  if (!vertexAlloc.valid || !indexAlloc.valid) {
    // Out of memory in our giant buffers!
    LOGZILLA_ERROR("Out of sub-allocated VRAM! Vertices: %d, Indices: %d", vertexAlloc.valid, indexAlloc.valid);
    if (vertexAlloc.valid) m_vertexAllocator.Free(vertexAlloc);
    if (indexAlloc.valid) m_indexAllocator.Free(indexAlloc);
    return;
  }

  ChunkRenderData renderData;
  renderData.x = mesh.x;
  renderData.y = mesh.y;
  renderData.z = mesh.z;
  renderData.minY = mesh.minY;
  renderData.maxY = mesh.maxY;
  renderData.indexCount = indexCount;
  renderData.vertexAllocation = vertexAlloc;
  renderData.indexAllocation = indexAlloc;

  // Vertex Buffer (Staging)
  VkDeviceSize vertexBufferSize = sizeof(mesh.vertices[0]) * vertexCount;
  VkBuffer vertexStagingBuffer;
  VkDeviceMemory vertexStagingBufferMemory;
  VulkanBuffer::CreateBuffer(m_context, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               vertexStagingBuffer, vertexStagingBufferMemory);
  
  void *vData;
  vkMapMemory(m_context.GetDevice(), vertexStagingBufferMemory, 0, vertexBufferSize, 0, &vData);
  memcpy(vData, mesh.vertices.data(), (size_t)vertexBufferSize);
  vkUnmapMemory(m_context.GetDevice(), vertexStagingBufferMemory);
  
  VkDeviceSize dstVertexOffset = vertexAlloc.start * sizeof(mc::render::Vertex);
  VulkanBuffer::CopyBuffer(m_context, vertexStagingBuffer,  m_globalVertexBuffer,  vertexBufferSize,  0,  dstVertexOffset, m_graphicsQueueMutex);
  
  vkDestroyBuffer(m_context.GetDevice(), vertexStagingBuffer, nullptr);
  vkFreeMemory(m_context.GetDevice(), vertexStagingBufferMemory, nullptr);

  // Index Buffer (Staging)
  VkDeviceSize indexBufferSize = sizeof(mesh.indices[0]) * indexCount;
  VkBuffer indexStagingBuffer;
  VkDeviceMemory indexStagingBufferMemory;
  VulkanBuffer::CreateBuffer(m_context, indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               indexStagingBuffer, indexStagingBufferMemory);
               
  void *iData;
  vkMapMemory(m_context.GetDevice(), indexStagingBufferMemory, 0, indexBufferSize, 0, &iData);
  memcpy(iData, mesh.indices.data(), (size_t)indexBufferSize);
  vkUnmapMemory(m_context.GetDevice(), indexStagingBufferMemory);
  
  VkDeviceSize dstIndexOffset = indexAlloc.start * sizeof(uint32_t);
  VulkanBuffer::CopyBuffer(m_context, indexStagingBuffer,  m_globalIndexBuffer,  indexBufferSize,  0,  dstIndexOffset, m_graphicsQueueMutex);
  
  vkDestroyBuffer(m_context.GetDevice(), indexStagingBuffer, nullptr);
  vkFreeMemory(m_context.GetDevice(), indexStagingBufferMemory, nullptr);

  std::lock_guard<std::mutex> lock(m_readyRenderDataMutex);
  m_readyRenderData.push_back(renderData);
}

} // namespace mc::render
