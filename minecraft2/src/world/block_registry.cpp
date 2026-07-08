#include "block_registry.h"
#include <algorithm>
#include <iostream>

namespace mc::world {

BlockRegistry::BlockRegistry() {
  // Initialize default blocks based on the textures the user provided
  RegisterBlock(BlockType::Stone, "stone.png");
  RegisterBlock(BlockType::Dirt, "dirt.png");
  // Grass has grass top, dirt bottom, and grass side (which doesn't exist separately, so we can use grass.png for top/sides, dirt for bottom, or if grass_side exists, use it. The user provided grass.png, dirt.png, stone.png).
  // Assuming grass.png is the top, and we'll use it for sides too if there's no grass_side.png
  RegisterBlock(BlockType::Grass, "grass.png", "grass.png", "dirt.png");
  RegisterBlock(BlockType::Air, "");
}

void BlockRegistry::AddTexture(const std::string &tex) {
  if (tex.empty())
    return;
  if (std::find(m_allTextures.begin(), m_allTextures.end(), tex) ==
      m_allTextures.end()) {
    m_allTextures.push_back(tex);
  }
}

void BlockRegistry::RegisterBlock(BlockType type, const std::string &top,
                                  const std::string &side,
                                  const std::string &bottom) {
  BlockTextureInfo info;
  info.topTexture = top;
  info.sideTexture = side;
  info.bottomTexture = bottom;
  m_registry[type] = info;

  AddTexture(top);
  AddTexture(side);
  AddTexture(bottom);
}

void BlockRegistry::RegisterBlock(BlockType type, const std::string &allSides) {
  RegisterBlock(type, allSides, allSides, allSides);
}

const BlockTextureInfo &BlockRegistry::GetTextureInfo(BlockType type) const {
  auto it = m_registry.find(type);
  if (it != m_registry.end()) {
    return it->second;
  }
  static BlockTextureInfo emptyInfo;
  return emptyInfo;
}

void BlockRegistry::UpdateTextureLayers(
    const std::unordered_map<std::string, int> &layerMap) {
  for (auto &pair : m_registry) {
    if (layerMap.count(pair.second.topTexture)) {
      pair.second.topLayer = layerMap.at(pair.second.topTexture);
    }
    if (layerMap.count(pair.second.sideTexture)) {
      pair.second.sideLayer = layerMap.at(pair.second.sideTexture);
    }
    if (layerMap.count(pair.second.bottomTexture)) {
      pair.second.bottomLayer = layerMap.at(pair.second.bottomTexture);
    }
  }
}

} // namespace mc::world
