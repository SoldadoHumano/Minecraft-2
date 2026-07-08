#pragma once
#include <glm/glm.hpp>

namespace mc::render {

struct Vertex {
  uint32_t data;
  uint32_t uvAndLayer; // [u: 8 bits][v: 8 bits][layer: 16 bits]
};

} // namespace mc::render
