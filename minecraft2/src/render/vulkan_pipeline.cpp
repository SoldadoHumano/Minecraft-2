#include "vulkan_pipeline.h"
#include "shader_compiler.h"
#include "vertex.h"
#include <array>
#include <string>

namespace mc::render {

static const std::string vertShaderCode = R"(
#version 450
layout(location = 0) in uint inData;
layout(location = 1) in uint inUVAndLayer;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    vec4 chunkPos;
} pushConstants;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragUVAndLayer;

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
    
    vec3 color = vec3(1.0);
    if (face == 0 || face == 1) color *= 0.8;
    else if (face == 4 || face == 5) color *= 0.6;
    else if (face == 3) color *= 0.4;
    
    fragColor = color;

    uint u = (inUVAndLayer >> 0) & 0xFF;
    uint v = (inUVAndLayer >> 8) & 0xFF;
    uint layer = (inUVAndLayer >> 16) & 0xFFFF;
    fragUVAndLayer = vec3(float(u), float(v), float(layer));
}
)";

static const std::string fragShaderCode = R"(
#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragUVAndLayer;

layout(binding = 1) uniform sampler2DArray texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragUVAndLayer);
    outColor = vec4(fragColor * texColor.rgb, texColor.a);
}
)";

static const std::string aabbVertShaderCode = R"(
#version 450
layout(location = 0) in vec3 inPos;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    vec4 minPos;
    vec4 maxPos;
} pc;

void main() {
    vec3 pos = mix(pc.minPos.xyz, pc.maxPos.xyz, inPos);
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 1.0);
}
)";

static const std::string aabbFragShaderCode = R"(
#version 450
void main() {
}
)";

VkShaderModule
VulkanPipeline::CreateShaderModule(VulkanContext &context,
                                   const std::vector<uint32_t> &code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size() * sizeof(uint32_t);
  createInfo.pCode = code.data();

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(context.GetDevice(), &createInfo, nullptr,
                           &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
  return shaderModule;
}

PipelineObjects VulkanPipeline::CreateGraphicsPipeline(
    VulkanContext &context, VkRenderPass renderPass,
    VkDescriptorSetLayout descriptorSetLayout, VkExtent2D extent) {
  auto vertSpv = ShaderCompiler::CompileGLSLToSPIRV(vertShaderCode,
                                                    VK_SHADER_STAGE_VERTEX_BIT);
  auto fragSpv = ShaderCompiler::CompileGLSLToSPIRV(
      fragShaderCode, VK_SHADER_STAGE_FRAGMENT_BIT);

  VkShaderModule vertShaderModule = CreateShaderModule(context, vertSpv);
  VkShaderModule fragShaderModule = CreateShaderModule(context, fragSpv);

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                    fragShaderStageInfo};

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32_UINT;
  attributeDescriptions[0].offset = offsetof(Vertex, data);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32_UINT;
  attributeDescriptions[1].offset = offsetof(Vertex, uvAndLayer);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)extent.width;
  viewport.height = (float)extent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = extent;

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
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
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
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  PipelineObjects objects{};

  if (vkCreatePipelineLayout(context.GetDevice(), &pipelineLayoutInfo, nullptr,
                             &objects.layout) != VK_SUCCESS) {
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
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                               VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = objects.layout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(context.GetDevice(), VK_NULL_HANDLE, 1,
                                &pipelineInfo, nullptr,
                                &objects.pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }

  vkDestroyShaderModule(context.GetDevice(), fragShaderModule, nullptr);
  vkDestroyShaderModule(context.GetDevice(), vertShaderModule, nullptr);

  return objects;
}

PipelineObjects
VulkanPipeline::CreateAABBPipeline(VulkanContext &context,
                                   VkRenderPass renderPass,
                                   VkDescriptorSetLayout descriptorSetLayout) {
  auto vertSpv = ShaderCompiler::CompileGLSLToSPIRV(aabbVertShaderCode,
                                                    VK_SHADER_STAGE_VERTEX_BIT);
  auto fragSpv = ShaderCompiler::CompileGLSLToSPIRV(
      aabbFragShaderCode, VK_SHADER_STAGE_FRAGMENT_BIT);

  VkShaderModule vertShaderModule = CreateShaderModule(context, vertSpv);
  VkShaderModule fragShaderModule = CreateShaderModule(context, fragSpv);

  VkPipelineShaderStageCreateInfo shaderStages[] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr}};

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(glm::vec3);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributeDescription{};
  attributeDescription.binding = 0;
  attributeDescription.location = 0;
  attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescription.offset = 0;

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = 1;
  vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
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
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
      0; // Disable color write for occlusion query!

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable =
      VK_FALSE; // We don't write depth, just test against it
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(glm::vec4) * 2; // minPos and maxPos

  PipelineObjects objects{};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts =
      &descriptorSetLayout; // Re-use the same UBO layout
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(context.GetDevice(), &pipelineLayoutInfo, nullptr,
                             &objects.layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create aabb pipeline layout!");
  }

  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                               VK_DYNAMIC_STATE_SCISSOR};
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
  pipelineInfo.layout = objects.layout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(context.GetDevice(), VK_NULL_HANDLE, 1,
                                &pipelineInfo, nullptr,
                                &objects.pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create aabb graphics pipeline!");
  }

  vkDestroyShaderModule(context.GetDevice(), fragShaderModule, nullptr);
  vkDestroyShaderModule(context.GetDevice(), vertShaderModule, nullptr);

  return objects;
}

PipelineObjects VulkanPipeline::CreateComputePipeline(VulkanContext& context,
                                                      VkDescriptorSetLayout descriptorSetLayout,
                                                      const std::vector<uint32_t>& computeCode) {
  PipelineObjects objects{};

  VkShaderModule compShaderModule = CreateShaderModule(context, computeCode);

  VkPipelineShaderStageCreateInfo compShaderStageInfo{};
  compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  compShaderStageInfo.module = compShaderModule;
  compShaderStageInfo.pName = "main";

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

  if (vkCreatePipelineLayout(context.GetDevice(), &pipelineLayoutInfo, nullptr,
                             &objects.layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create compute pipeline layout!");
  }

  VkComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.layout = objects.layout;
  pipelineInfo.stage = compShaderStageInfo;

  if (vkCreateComputePipelines(context.GetDevice(), VK_NULL_HANDLE, 1,
                               &pipelineInfo, nullptr,
                               &objects.pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create compute pipeline!");
  }

  vkDestroyShaderModule(context.GetDevice(), compShaderModule, nullptr);

  return objects;
}

} // namespace mc::render
