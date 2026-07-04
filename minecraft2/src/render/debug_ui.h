#pragma once
#include "vulkan_context.h"
#include "../core/metrics.h"
#include <vector>
#include <string>

namespace mc::render {

class DebugUI {
public:
    DebugUI(VulkanContext& context, VkRenderPass renderPass, uint32_t width, uint32_t height);
    ~DebugUI();

    void Resize(uint32_t width, uint32_t height);
    bool HandleClick(double mouseX, double mouseY, mc::core::EngineMetrics& metrics);
    void HandleDrag(double mouseX, double mouseY, mc::core::EngineMetrics& metrics);
    void Draw(VkCommandBuffer cmd, mc::core::EngineMetrics& metrics);

private:
    VulkanContext& m_context;
    uint32_t m_width, m_height;

    struct UI_Vertex {
        float pos[2];
        float uv[2];
        float color[4];
    };

    // Textures (0 = Font, 1 = Crosshair SVG)
    VkImage m_atlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_atlasMemory = VK_NULL_HANDLE;
    VkImageView m_atlasView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // Pipeline
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // Dynamic Vertex Buffer
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    void* m_vertexBufferMapped = nullptr;
    uint32_t m_maxVertices = 4096;

    // stb_truetype baked char data (for ASCII 32..126)
    struct BakedChar {
        unsigned short x0,y0,x1,y1;
        float xoff,yoff,xadvance;
    };
    std::vector<BakedChar> m_charData;
    
    // SVG UVs in the atlas
    float m_crosshairUV[4]; // minX, minY, maxX, maxY
    float m_menuBgUV[4];
    float m_btnPlusUV[4];
    float m_btnMinusUV[4];

    void BuildAtlas();
    void CreatePipeline(VkRenderPass renderPass);
    void CreateBuffers();
    void UpdateBuffer(const std::vector<UI_Vertex>& vertices);
    
    // Helper to upload image to Vulkan
    void UploadTexture(unsigned char* pixels, uint32_t width, uint32_t height);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
};

} // namespace mc::render
