#include "shader_compiler.h"
#include <stdexcept>
#include <iostream>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>

namespace mc::render {

void ShaderCompiler::Initialize() {
    glslang::InitializeProcess();
}

void ShaderCompiler::Finalize() {
    glslang::FinalizeProcess();
}

std::vector<uint32_t> ShaderCompiler::CompileGLSLToSPIRV(const std::string& glslSource, VkShaderStageFlagBits stage) {
    EShLanguage language = EShLangVertex;
    if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        language = EShLangFragment;
    } else if (stage != VK_SHADER_STAGE_VERTEX_BIT) {
        throw std::runtime_error("Unsupported shader stage");
    }

    glslang::TShader shader(language);
    const char* sourceStrings[1] = { glslSource.c_str() };
    shader.setStrings(sourceStrings, 1);
    
    int clientInputSemanticsVersion = 100; // Vulkan 1.0 semantics
    glslang::EShTargetClientVersion vulkanClientVersion = glslang::EShTargetVulkan_1_3;
    glslang::EShTargetLanguageVersion targetVersion = glslang::EShTargetSpv_1_0;

    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, clientInputSemanticsVersion);
    shader.setEnvClient(glslang::EShClientVulkan, vulkanClientVersion);
    shader.setEnvTarget(glslang::EShTargetSpv, targetVersion);

    const TBuiltInResource* resources = GetDefaultResources();

    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(resources, 100, false, messages)) {
        std::cerr << "GLSL Parsing Failed:\n" << shader.getInfoLog() << "\n" << shader.getInfoDebugLog() << "\n";
        throw std::runtime_error("Shader compilation failed.");
    }

    glslang::TProgram program;
    program.addShader(&shader);
    
    if (!program.link(messages)) {
        std::cerr << "GLSL Linking Failed:\n" << program.getInfoLog() << "\n" << program.getInfoDebugLog() << "\n";
        throw std::runtime_error("Shader linking failed.");
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(language), spirv);
    
    return spirv;
}

} // namespace mc::render
