#pragma once
#include "../core/metrics.h"
#include <volk.h>

namespace mc::render {

class DebugUI {
public:
  DebugUI() = default;
  ~DebugUI() = default;

  // Render the ImGui interfaces. Should be called inside the main loop before ImGui::Render()
  void Draw(mc::core::EngineMetrics &metrics);
};

} // namespace mc::render
