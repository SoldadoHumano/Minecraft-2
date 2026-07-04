#pragma once
#include <cstdint>

namespace mc::core {

struct EngineMetrics {
    float fps = 0.0f;
    float frameTimeMs = 0.0f;
    float tps = 0.0f;
    uint32_t verticesRendered = 0;
    uint32_t chunksLoaded = 0;
    uint32_t chunksRendered = 0;
    bool showF3 = false;
    bool isPaused = false;
    int viewDistance = 12;
};

} // namespace mc::core
