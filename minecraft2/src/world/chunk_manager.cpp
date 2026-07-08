#include "chunk_manager.h"
#include "../core/job_system.h"
#include "../core/profiler.h"
#include <iostream>

namespace mc::world {

ChunkManager::ChunkManager(uint32_t seed) : m_seed(seed), m_noise(seed) {}

ChunkManager::~ChunkManager() {}

void ChunkManager::Clear() {
  std::lock_guard<std::mutex> lock(m_chunkMutex);
  for (const auto &pair : m_chunks) {
    if (m_chunkUnloadCallback) {
      m_chunkUnloadCallback(pair.first.x, pair.first.y);
    }
  }
  m_chunks.clear();
}

void ChunkManager::Update(int playerChunkX, int playerChunkZ,
                          int viewDistance) {
  PROFILE_SCOPE("ChunkManager::Update");
  int loadDistance = viewDistance + 1; // +1 to prevent border flapping

  // Unload chunks outside load distance
  std::vector<glm::ivec2> toRemove;
  {
    std::lock_guard<std::mutex> lock(m_chunkMutex);
    for (const auto &pair : m_chunks) {
      int dx = std::abs(pair.first.x - playerChunkX);
      int dz = std::abs(pair.first.y - playerChunkZ);
      if (dx > loadDistance || dz > loadDistance) {
        toRemove.push_back(pair.first);
      }
    }
    for (const auto &pos : toRemove) {
      m_chunks.erase(pos);
      if (m_chunkUnloadCallback) {
        m_chunkUnloadCallback(pos.x, pos.y);
      }
    }
  }

  // Load chunks inside load distance (Radial Order)
  std::vector<glm::ivec2> chunksToLoad;
  for (int x = -loadDistance; x <= loadDistance; ++x) {
    for (int z = -loadDistance; z <= loadDistance; ++z) {
      chunksToLoad.push_back(glm::ivec2(playerChunkX + x, playerChunkZ + z));
    }
  }

  // Sort radially from player position
  std::sort(chunksToLoad.begin(), chunksToLoad.end(),
            [playerChunkX, playerChunkZ](const glm::ivec2 &a, const glm::ivec2 &b) {
              int dxA = a.x - playerChunkX;
              int dzA = a.y - playerChunkZ;
              int distA = dxA * dxA + dzA * dzA;
              
              int dxB = b.x - playerChunkX;
              int dzB = b.y - playerChunkZ;
              int distB = dxB * dxB + dzB * dzB;
              
              return distA < distB;
            });

  for (const auto &pos : chunksToLoad) {
    bool needsLoad = false;
    {
      std::lock_guard<std::mutex> lock(m_chunkMutex);
      if (m_chunks.find(pos) == m_chunks.end()) {
        m_chunks[pos] = {nullptr, ChunkState::LOADING};
        needsLoad = true;
      }
    }

    if (needsLoad) {
      RequestChunkLoad(pos.x, pos.y);
    }
  }
}

void ChunkManager::GetVoxelGridData(int playerChunkX, int playerChunkZ, int radius, std::vector<uint8_t>& outBlocks, int& outSizeX, int& outSizeY, int& outSizeZ, int& outOffsetX, int& outOffsetZ) {
    std::lock_guard<std::mutex> lock(m_chunkMutex);
    
    int numChunksX = radius * 2 + 1;
    int numChunksZ = radius * 2 + 1;
    
    outSizeX = numChunksX * CHUNK_WIDTH;
    outSizeY = CHUNK_HEIGHT;
    outSizeZ = numChunksZ * CHUNK_DEPTH;
    
    outOffsetX = playerChunkX - radius;
    outOffsetZ = playerChunkZ - radius;
    
    size_t totalBytes = outSizeX * outSizeY * outSizeZ;
    if (outBlocks.size() != totalBytes) {
        outBlocks.resize(totalBytes, 0);
    } else {
        std::fill(outBlocks.begin(), outBlocks.end(), 0); // Clear to air
    }
    
    for (int cz = 0; cz < numChunksZ; ++cz) {
        for (int cx = 0; cx < numChunksX; ++cx) {
            int worldChunkX = outOffsetX + cx;
            int worldChunkZ = outOffsetZ + cz;
            
            auto it = m_chunks.find(glm::ivec2(worldChunkX, worldChunkZ));
            if (it != m_chunks.end() && it->second.chunk && it->second.state != ChunkState::LOADING) {
                // Copy blocks
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    for (int z = 0; z < CHUNK_DEPTH; ++z) {
                        for (int x = 0; x < CHUNK_WIDTH; ++x) {
                            int localIndex = y + z * CHUNK_HEIGHT + x * CHUNK_HEIGHT * CHUNK_DEPTH; // From Chunk::GetIndex
                            uint8_t blockId = static_cast<uint8_t>(it->second.chunk->GetBlock(x, y, z));
                            
                            int globalX = cx * CHUNK_WIDTH + x;
                            int globalZ = cz * CHUNK_DEPTH + z;
                            int globalIndex = globalX + outSizeX * (y + outSizeY * globalZ);
                            
                            outBlocks[globalIndex] = blockId;
                        }
                    }
                }
            }
        }
    }
}

void ChunkManager::RequestChunkLoad(int cx, int cz) {
  mc::core::JobSystem::Execute([this, cx, cz]() {
    PROFILE_SCOPE("Chunk Generation");
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
        if (it == m_chunks.end() || it->second.state != ChunkState::GENERATED)
          return false;

        const glm::ivec2 offsets[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4; i++) {
          auto nIt =
              m_chunks.find(glm::ivec2(nx + offsets[i].x, nz + offsets[i].y));
          if (nIt == m_chunks.end() || nIt->second.state == ChunkState::LOADING)
            return false;
        }
        return true;
      };

      // Check self
      if (checkMeshReady(cx, cz)) {
        m_chunks[glm::ivec2(cx, cz)].state = ChunkState::MESHED;
        readyToMesh.push_back(glm::ivec2(cx, cz));
      }
      // Check neighbors
      const glm::ivec2 offsets[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (int i = 0; i < 4; i++) {
        if (checkMeshReady(cx + offsets[i].x, cz + offsets[i].y)) {
          m_chunks[glm::ivec2(cx + offsets[i].x, cz + offsets[i].y)].state =
              ChunkState::MESHED;
          readyToMesh.push_back(
              glm::ivec2(cx + offsets[i].x, cz + offsets[i].y));
        }
      }
    }

    for (const auto &pos : readyToMesh) {
      RequestChunkMesh(pos.x, pos.y);
    }
  });
}

void ChunkManager::RequestChunkMesh(int cx, int cz) {
  mc::core::JobSystem::Execute([this, cx, cz]() {
    PROFILE_SCOPE("Chunk Meshing");
    std::shared_ptr<Chunk> chunk, nXP, nXM, nZP, nZM;
    {
      std::lock_guard<std::mutex> lock(m_chunkMutex);
      auto it = m_chunks.find(glm::ivec2(cx, cz));
      if (it == m_chunks.end() || !it->second.chunk)
        return;
      chunk = it->second.chunk;

      auto getNeighbor = [&](int nx, int nz) -> std::shared_ptr<Chunk> {
        auto nIt = m_chunks.find(glm::ivec2(nx, nz));
        if (nIt != m_chunks.end())
          return nIt->second.chunk;
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
      bool stillValid = false;
      {
        std::lock_guard<std::mutex> lock(m_chunkMutex);
        stillValid = (m_chunks.find(glm::ivec2(cx, cz)) != m_chunks.end());
      }
      
      if (stillValid) {
        for (auto &mesh : meshes) {
          m_meshReadyCallback(mesh);
        }
      }
    }
  });
}

} // namespace mc::world
