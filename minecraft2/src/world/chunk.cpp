#include "chunk.h"
#include "../math/noise.h"
#include "chunk_manager.h"
#include <iostream>
namespace mc::world {

// Precomputed face vertices for a 1x1x1 cube at origin (0,0,0) to (1,1,1)
const glm::vec3 faceVertices[6][4] = {
    // +X Right (x=1)
    {{1.0f, 0.0f, 1.0f},
     {1.0f, 0.0f, 0.0f},
     {1.0f, 1.0f, 0.0f},
     {1.0f, 1.0f, 1.0f}},
    // -X Left (x=0)
    {{0.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 1.0f},
     {0.0f, 1.0f, 1.0f},
     {0.0f, 1.0f, 0.0f}},
    // +Y Top (y=1)
    {{0.0f, 1.0f, 1.0f},
     {1.0f, 1.0f, 1.0f},
     {1.0f, 1.0f, 0.0f},
     {0.0f, 1.0f, 0.0f}},
    // -Y Bottom (y=0)
    {{0.0f, 0.0f, 0.0f},
     {1.0f, 0.0f, 0.0f},
     {1.0f, 0.0f, 1.0f},
     {0.0f, 0.0f, 1.0f}},
    // +Z Front (z=1)
    {{0.0f, 0.0f, 1.0f},
     {1.0f, 0.0f, 1.0f},
     {1.0f, 1.0f, 1.0f},
     {0.0f, 1.0f, 1.0f}},
    // -Z Back (z=0)
    {{1.0f, 0.0f, 0.0f},
     {0.0f, 0.0f, 0.0f},
     {0.0f, 1.0f, 0.0f},
     {1.0f, 1.0f, 0.0f}}};

Chunk::Chunk(int chunkX, int chunkZ) : m_chunkX(chunkX), m_chunkZ(chunkZ) {
  memset(m_blocks, 0, sizeof(m_blocks)); // BlockType::Air is 0
}

#include <immintrin.h>

void Chunk::Generate(const mc::math::PerlinNoise &noise) {
  alignas(32) float worldX_arr[CHUNK_WIDTH * CHUNK_DEPTH];
  alignas(32) float worldZ_arr[CHUNK_WIDTH * CHUNK_DEPTH];

  for (int x = 0; x < CHUNK_WIDTH; ++x) {
    for (int z = 0; z < CHUNK_DEPTH; ++z) {
      int idx = x * CHUNK_DEPTH + z;
      worldX_arr[idx] = static_cast<float>(m_chunkX * CHUNK_WIDTH + x) * 0.01f;
      worldZ_arr[idx] = static_cast<float>(m_chunkZ * CHUNK_DEPTH + z) * 0.01f;
    }
  }

  int totalBlocks = CHUNK_WIDTH * CHUNK_DEPTH;
  for (int i = 0; i < totalBlocks; i += 8) {
    __m256 vx = _mm256_loadu_ps(&worldX_arr[i]);
    __m256 vz = _mm256_loadu_ps(&worldZ_arr[i]);
    __m256 vNoise = noise.Fractal2D_AVX2(vx, vz, 4, 0.5f, 2.0f);
    
    // height = 64 + (noiseVal * 32.0f)
    __m256 vHeightF = _mm256_fmadd_ps(vNoise, _mm256_set1_ps(32.0f), _mm256_set1_ps(64.0f));
    
    // Bound heights between 1 and CHUNK_HEIGHT - 1 (255)
    vHeightF = _mm256_max_ps(vHeightF, _mm256_set1_ps(1.0f));
    vHeightF = _mm256_min_ps(vHeightF, _mm256_set1_ps(255.0f));
    
    // Convert to integers
    __m256i vHeight = _mm256_cvttps_epi32(vHeightF);

    alignas(32) int heights[8];
    _mm256_store_si256((__m256i*)heights, vHeight);

    for (int j = 0; j < 8; ++j) {
      int x = (i + j) / CHUNK_DEPTH;
      int z = (i + j) % CHUNK_DEPTH;
      int h = heights[j];

      BlockType* col = &m_blocks[GetIndex(x, 0, z)];
      int stoneHeight = h > 3 ? h - 3 : 0;
      int y = 0;

      // Fill stone using 256-bit vectors (32 blocks per write)
      __m256i vStone = _mm256_set1_epi8(static_cast<char>(BlockType::Stone));
      for (; y + 31 < stoneHeight; y += 32) {
        _mm256_storeu_si256((__m256i*)&col[y], vStone);
      }
      for (; y < stoneHeight; ++y) {
        col[y] = BlockType::Stone;
      }
      for (; y < h - 1; ++y) {
        col[y] = BlockType::Dirt;
      }
      col[h - 1] = BlockType::Grass;
    }
  }
}

BlockType Chunk::GetBlock(int x, int y, int z) const {
  if (x < 0 || x >= CHUNK_WIDTH || y < 0 || y >= CHUNK_HEIGHT || z < 0 ||
      z >= CHUNK_DEPTH) {
    return BlockType::Air; // Out of bounds is treated as Air internally
  }
  return m_blocks[GetIndex(x, y, z)];
}

void Chunk::SetNeighbors(std::shared_ptr<Chunk> nxp, std::shared_ptr<Chunk> nxm,
                         std::shared_ptr<Chunk> nzp,
                         std::shared_ptr<Chunk> nzm) {
  m_neighborXP = nxp;
  m_neighborXM = nxm;
  m_neighborZP = nzp;
  m_neighborZM = nzm;
}

void Chunk::SetBlock(int x, int y, int z, BlockType type) {
  if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 &&
      z < CHUNK_DEPTH) {
    m_blocks[GetIndex(x, y, z)] = type;
  }
}

bool Chunk::IsFaceVisible(int x, int y, int z) const {
  if (y < 0 || y >= CHUNK_HEIGHT)
    return true; // Always draw top/bottom borders

  if (x < 0) {
    if (m_neighborXM)
      return m_neighborXM->GetBlock(CHUNK_WIDTH - 1, y, z) == BlockType::Air;
    return true;
  }
  if (x >= CHUNK_WIDTH) {
    if (m_neighborXP)
      return m_neighborXP->GetBlock(0, y, z) == BlockType::Air;
    return true;
  }
  if (z < 0) {
    if (m_neighborZM)
      return m_neighborZM->GetBlock(x, y, CHUNK_DEPTH - 1) == BlockType::Air;
    return true;
  }
  if (z >= CHUNK_DEPTH) {
    if (m_neighborZP)
      return m_neighborZP->GetBlock(x, y, 0) == BlockType::Air;
    return true;
  }

  return GetBlock(x, y, z) == BlockType::Air;
}

void Chunk::AddGreedyQuad(std::vector<mc::render::Vertex> &vertices,
                          std::vector<uint32_t> &indices, int x, int y, int z,
                          int face, BlockType type, int wx, int wy, int wz) {
  uint32_t indexOffset = static_cast<uint32_t>(vertices.size());
  uint32_t packedFace = (face & 0x7) << 18;
  uint32_t packedType = (static_cast<uint32_t>(type) & 0xFF) << 21;

  glm::vec3 p[4];
  if (face == 0) { // +X
    p[0] = glm::vec3(x + 1, y, z + wz);
    p[1] = glm::vec3(x + 1, y, z);
    p[2] = glm::vec3(x + 1, y + wy, z);
    p[3] = glm::vec3(x + 1, y + wy, z + wz);
  } else if (face == 1) { // -X
    p[0] = glm::vec3(x, y, z);
    p[1] = glm::vec3(x, y, z + wz);
    p[2] = glm::vec3(x, y + wy, z + wz);
    p[3] = glm::vec3(x, y + wy, z);
  } else if (face == 2) { // +Y
    p[0] = glm::vec3(x, y + 1, z + wz);
    p[1] = glm::vec3(x + wx, y + 1, z + wz);
    p[2] = glm::vec3(x + wx, y + 1, z);
    p[3] = glm::vec3(x, y + 1, z);
  } else if (face == 3) { // -Y
    p[0] = glm::vec3(x, y, z);
    p[1] = glm::vec3(x + wx, y, z);
    p[2] = glm::vec3(x + wx, y, z + wz);
    p[3] = glm::vec3(x, y, z + wz);
  } else if (face == 4) { // +Z
    p[0] = glm::vec3(x, y, z + 1);
    p[1] = glm::vec3(x + wx, y, z + 1);
    p[2] = glm::vec3(x + wx, y + wy, z + 1);
    p[3] = glm::vec3(x, y + wy, z + 1);
  } else if (face == 5) { // -Z
    p[0] = glm::vec3(x + wx, y, z);
    p[1] = glm::vec3(x, y, z);
    p[2] = glm::vec3(x, y + wy, z);
    p[3] = glm::vec3(x + wx, y + wy, z);
  }

  for (int i = 0; i < 4; ++i) {
    uint32_t cx = static_cast<uint32_t>(p[i].x) & 0x1F;
    uint32_t cy = static_cast<uint32_t>(p[i].y) & 0xFF;
    uint32_t cz = static_cast<uint32_t>(p[i].z) & 0x1F;

    uint32_t cornerPackedXYZ = cx | (cy << 5) | (cz << 13);
    uint32_t finalData = cornerPackedXYZ | packedFace | packedType;
    vertices.push_back({finalData});
  }

  indices.push_back(indexOffset + 0);
  indices.push_back(indexOffset + 1);
  indices.push_back(indexOffset + 2);
  indices.push_back(indexOffset + 0);
  indices.push_back(indexOffset + 2);
  indices.push_back(indexOffset + 3);
}

struct MaskBlock {
  bool visible;
  BlockType type;
};

void Chunk::BuildMeshes(std::vector<struct ChunkMesh> &outMeshes) {
  std::vector<mc::render::Vertex> vertices;
  std::vector<uint32_t> indices;

  int meshMinY = 255;
  int meshMaxY = 0;

  for (int sectionY = 0; sectionY < 16; ++sectionY) {
    int startY = sectionY * 16;
    size_t indicesBefore = indices.size();
    MaskBlock mask[16][16];

    auto buildGreedyFace = [&](int face) {
      for (int slice = 0; slice < 16; ++slice) {
        // Populate mask
        for (int u = 0; u < 16; ++u) {
          for (int v = 0; v < 16; ++v) {
            int x = 0, y = 0, z = 0;
            int nx = 0, ny = 0, nz = 0;
            if (face == 0) {
              x = slice;
              y = startY + u;
              z = v;
              nx = x + 1;
              ny = y;
              nz = z;
            } else if (face == 1) {
              x = slice;
              y = startY + u;
              z = v;
              nx = x - 1;
              ny = y;
              nz = z;
            } else if (face == 2) {
              y = startY + slice;
              x = u;
              z = v;
              nx = x;
              ny = y + 1;
              nz = z;
            } else if (face == 3) {
              y = startY + slice;
              x = u;
              z = v;
              nx = x;
              ny = y - 1;
              nz = z;
            } else if (face == 4) {
              z = slice;
              x = u;
              y = startY + v;
              nx = x;
              ny = y;
              nz = z + 1;
            } else if (face == 5) {
              z = slice;
              x = u;
              y = startY + v;
              nx = x;
              ny = y;
              nz = z - 1;
            }

            BlockType type = GetBlock(x, y, z);
            if (type != BlockType::Air && IsFaceVisible(nx, ny, nz)) {
              mask[u][v] = {true, type};
            } else {
              mask[u][v] = {false, BlockType::Air};
            }
          }
        }

        // Greedy sweep
        for (int u = 0; u < 16; ++u) {
          for (int v = 0; v < 16; ++v) {
            if (mask[u][v].visible) {
              BlockType type = mask[u][v].type;
              int w = 1, h = 1;

              while (v + w < 16 && mask[u][v + w].visible &&
                     mask[u][v + w].type == type) {
                w++;
              }

              bool done = false;
              while (u + h < 16) {
                for (int k = 0; k < w; ++k) {
                  if (!mask[u + h][v + k].visible ||
                      mask[u + h][v + k].type != type) {
                    done = true;
                    break;
                  }
                }
                if (done)
                  break;
                h++;
              }

              int x = 0, y = 0, z = 0;
              if (face == 0 || face == 1) {
                x = slice;
                y = u + startY;
                z = v;
              } else if (face == 2 || face == 3) {
                x = u;
                y = slice + startY;
                z = v;
              } else if (face == 4 || face == 5) {
                x = u;
                y = v + startY;
                z = slice;
              }

              int wx = 1, wy = 1, wz = 1;
              if (face == 0 || face == 1) {
                wy = h;
                wz = w;
              } else if (face == 2 || face == 3) {
                wx = h;
                wz = w;
              } else if (face == 4 || face == 5) {
                wx = h;
                wy = w;
              }

              AddGreedyQuad(vertices, indices, x, y, z, face, type, wx, wy, wz);

              for (int cu = 0; cu < h; ++cu) {
                for (int cv = 0; cv < w; ++cv) {
                  mask[u + cu][v + cv].visible = false;
                }
              }
            }
          }
        }
      }
    };

    buildGreedyFace(0);
    buildGreedyFace(1);
    buildGreedyFace(2);
    buildGreedyFace(3);
    buildGreedyFace(4);
    buildGreedyFace(5);

    if (indices.size() > indicesBefore) {
      if (startY < meshMinY)
        meshMinY = startY;
      if (startY + 16 > meshMaxY)
        meshMaxY = startY + 16;
    }
  }

  if (!indices.empty()) {
    ChunkMesh mesh;
    mesh.x = m_chunkX;
    mesh.y = 0;
    mesh.z = m_chunkZ;
    mesh.minY = meshMinY;
    mesh.maxY = meshMaxY;
    mesh.vertices = std::move(vertices);
    mesh.indices = std::move(indices);
    outMeshes.push_back(std::move(mesh));
  }

  // Break circular reference to prevent memory leaks
  m_neighborXP = nullptr;
  m_neighborXM = nullptr;
  m_neighborZP = nullptr;
  m_neighborZM = nullptr;
}

} // namespace mc::world
