#include "chunk_manager.h"
#include "../core/job_system.h"
#include <iostream>

namespace mc::world {

ChunkManager::ChunkManager(uint32_t seed) : m_seed(seed), m_noise(seed) {
}

ChunkManager::~ChunkManager() {
}

void ChunkManager::Update(int playerChunkX, int playerChunkZ, int viewDistance) {
    // Unload chunks outside view distance
    std::vector<glm::ivec2> toRemove;
    {
        std::lock_guard<std::mutex> lock(m_chunkMutex);
        for (const auto& pair : m_chunks) {
            int dx = std::abs(pair.first.x - playerChunkX);
            int dz = std::abs(pair.first.y - playerChunkZ);
            if (dx > viewDistance || dz > viewDistance) {
                toRemove.push_back(pair.first);
            }
        }
        for (const auto& pos : toRemove) {
            m_chunks.erase(pos);
            if (m_chunkUnloadCallback) {
                m_chunkUnloadCallback(pos.x, pos.y);
            }
        }
    }

    // Load chunks inside view distance
    for (int x = -viewDistance; x <= viewDistance; ++x) {
        for (int z = -viewDistance; z <= viewDistance; ++z) {
                int cx = playerChunkX + x;
                int cz = playerChunkZ + z;
                glm::ivec2 pos(cx, cz);
                
                bool needsLoad = false;
                {
                    std::lock_guard<std::mutex> lock(m_chunkMutex);
                    if (m_chunks.find(pos) == m_chunks.end()) {
                        m_chunks[pos] = {nullptr, ChunkState::LOADING};
                        needsLoad = true;
                    }
                }
                
                if (needsLoad) {
                    RequestChunkLoad(cx, cz);
                }
        }
    }
}

void ChunkManager::RequestChunkLoad(int cx, int cz) {
    mc::core::JobSystem::Execute([this, cx, cz]() {
        auto chunk = std::make_shared<Chunk>(cx, cz);
        chunk->Generate(m_noise);

        std::vector<glm::ivec2> readyToMesh;

        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            if (m_chunks.find(glm::ivec2(cx, cz)) == m_chunks.end()) {
                return; // Chunk was unloaded while generating
            }
            m_chunks[glm::ivec2(cx, cz)] = {chunk, ChunkState::GENERATED};

            auto checkMeshReady = [&](int nx, int nz) {
                auto it = m_chunks.find(glm::ivec2(nx, nz));
                if (it == m_chunks.end() || it->second.state != ChunkState::GENERATED) return false;
                
                const glm::ivec2 offsets[4] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
                for (int i=0; i<4; i++) {
                    auto nIt = m_chunks.find(glm::ivec2(nx + offsets[i].x, nz + offsets[i].y));
                    if (nIt == m_chunks.end() || nIt->second.state == ChunkState::LOADING) return false;
                }
                return true;
            };

            // Check self
            if (checkMeshReady(cx, cz)) {
                m_chunks[glm::ivec2(cx, cz)].state = ChunkState::MESHED;
                readyToMesh.push_back(glm::ivec2(cx, cz));
            }
            // Check neighbors
            const glm::ivec2 offsets[4] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
            for (int i=0; i<4; i++) {
                if (checkMeshReady(cx + offsets[i].x, cz + offsets[i].y)) {
                    m_chunks[glm::ivec2(cx + offsets[i].x, cz + offsets[i].y)].state = ChunkState::MESHED;
                    readyToMesh.push_back(glm::ivec2(cx + offsets[i].x, cz + offsets[i].y));
                }
            }
        }

        for (const auto& pos : readyToMesh) {
            RequestChunkMesh(pos.x, pos.y);
        }
    });
}

void ChunkManager::RequestChunkMesh(int cx, int cz) {
    mc::core::JobSystem::Execute([this, cx, cz]() {
        std::shared_ptr<Chunk> chunk, nXP, nXM, nZP, nZM;
        {
            std::lock_guard<std::mutex> lock(m_chunkMutex);
            auto it = m_chunks.find(glm::ivec2(cx, cz));
            if (it == m_chunks.end() || !it->second.chunk) return;
            chunk = it->second.chunk;

            auto getNeighbor = [&](int nx, int nz) -> std::shared_ptr<Chunk> {
                auto nIt = m_chunks.find(glm::ivec2(nx, nz));
                if (nIt != m_chunks.end()) return nIt->second.chunk;
                return nullptr;
            };
            nXP = getNeighbor(cx + 1, cz);
            nXM = getNeighbor(cx - 1, cz);
            nZP = getNeighbor(cx, cz + 1);
            nZM = getNeighbor(cx, cz - 1);
        }

        chunk->SetNeighbors(nXP, nXM, nZP, nZM);

        std::vector<ChunkMesh> meshes;
        chunk->BuildMeshes(meshes);

        if (m_meshReadyCallback) {
            for (auto& mesh : meshes) {
                m_meshReadyCallback(mesh);
            }
        }
    });
}

} // namespace mc::world
