#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <volk.h>

namespace mc::render {

class ShaderCompiler {
public:
    static void Initialize();
    static void Finalize();

    static std::vector<uint32_t> CompileGLSLToSPIRV(const std::string& glslSource, VkShaderStageFlagBits stage);
};

} // namespace mc::render
