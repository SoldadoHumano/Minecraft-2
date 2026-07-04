#pragma once
#include "chunk.h"
#include "../math/noise.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

namespace mc::world {

struct ChunkMesh {
    int x, y, z;
    int minY = 255;
    int maxY = 0;
    std::vector<mc::render::Vertex> vertices;
    std::vector<uint32_t> indices;
};

class ChunkManager {
public:
    ChunkManager(uint32_t seed);
    ~ChunkManager();

    void Update(int playerChunkX, int playerChunkZ, int viewDistance);
    
    // Returns meshes that background threads have finished building
    void SetMeshReadyCallback(std::function<void(const ChunkMesh&)> callback) { m_meshReadyCallback = callback; }
    void SetChunkUnloadCallback(std::function<void(int, int)> callback) { m_chunkUnloadCallback = callback; }

private:
    void RequestChunkLoad(int cx, int cz);
    void RequestChunkMesh(int cx, int cz);

    enum class ChunkState {
        LOADING,
        GENERATED,
        MESHED
    };

    struct ChunkRecord {
        std::shared_ptr<Chunk> chunk;
        ChunkState state;
    };

    std::unordered_map<glm::ivec2, ChunkRecord> m_chunks;
    std::mutex m_chunkMutex;

    std::function<void(const ChunkMesh&)> m_meshReadyCallback;
    std::function<void(int, int)> m_chunkUnloadCallback;

    uint32_t m_seed;
    mc::math::PerlinNoise m_noise;
};

} // namespace mc::world
