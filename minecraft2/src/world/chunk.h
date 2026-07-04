#pragma once
#include "../math/noise.h"
#include "../render/vertex.h"
#include "block.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace mc::world {

constexpr int CHUNK_WIDTH = 16;
constexpr int CHUNK_HEIGHT = 256;
constexpr int CHUNK_DEPTH = 16;

class Chunk {
public:
  Chunk(int chunkX, int chunkZ);

  void Generate(const mc::math::PerlinNoise &noise);
  void BuildMeshes(std::vector<struct ChunkMesh> &outMeshes);

  BlockType GetBlock(int x, int y, int z) const;
  void SetBlock(int x, int y, int z, BlockType type);

  void SetNeighbors(std::shared_ptr<Chunk> nxp, std::shared_ptr<Chunk> nxm,
                    std::shared_ptr<Chunk> nzp, std::shared_ptr<Chunk> nzm);

private:
  int m_chunkX, m_chunkZ;
  BlockType m_blocks[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH];

  std::shared_ptr<Chunk> m_neighborXP;
  std::shared_ptr<Chunk> m_neighborXM;
  std::shared_ptr<Chunk> m_neighborZP;
  std::shared_ptr<Chunk> m_neighborZM;

  inline int GetIndex(int x, int y, int z) const {
    return y + z * CHUNK_HEIGHT + x * CHUNK_HEIGHT * CHUNK_DEPTH;
  }

  bool IsFaceVisible(int x, int y, int z) const;
  void AddGreedyQuad(std::vector<mc::render::Vertex> &vertices,
                     std::vector<uint32_t> &indices, int x, int y, int z,
                     int face, BlockType type, int wx, int wy, int wz);
};

} // namespace mc::world
