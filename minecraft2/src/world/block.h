#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace mc::world {

enum class BlockType : uint8_t {
    Air = 0,
    Stone = 1,
    Dirt = 2,
    Grass = 3
};

inline glm::vec3 GetBlockColor(BlockType type) {
    switch (type) {
        case BlockType::Stone: return {0.5f, 0.5f, 0.5f};
        case BlockType::Dirt:  return {0.4f, 0.25f, 0.1f};
        case BlockType::Grass: return {0.2f, 0.7f, 0.1f};
        default: return {1.0f, 1.0f, 1.0f};
    }
}

} // namespace mc::world
