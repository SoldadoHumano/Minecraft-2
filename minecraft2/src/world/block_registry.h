#pragma once
#include "block.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace mc::world {

struct BlockTextureInfo {
  std::string topTexture;
  std::string sideTexture;
  std::string bottomTexture;

  int topLayer = 0;
  int sideLayer = 0;
  int bottomLayer = 0;
};

class BlockRegistry {
public:
  static BlockRegistry &Get() {
    static BlockRegistry instance;
    return instance;
  }

  void RegisterBlock(BlockType type, const std::string &top,
                     const std::string &side, const std::string &bottom);
  void RegisterBlock(BlockType type, const std::string &allSides);

  const BlockTextureInfo &GetTextureInfo(BlockType type) const;

  // Called after textures are loaded to set the layer indices
  void UpdateTextureLayers(const std::unordered_map<std::string, int> &layerMap);

  const std::vector<std::string>& GetAllTextures() const { return m_allTextures; }

private:
  BlockRegistry();

  void AddTexture(const std::string& tex);

  std::unordered_map<BlockType, BlockTextureInfo> m_registry;
  std::vector<std::string> m_allTextures;
};

} // namespace mc::world
