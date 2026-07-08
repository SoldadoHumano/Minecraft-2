#include "debug_ui.h"
#include <imgui.h>
#include <string>
#include <functional>
#include "../core/profiler.h"

namespace mc::render {

void DebugUI::Draw(mc::core::EngineMetrics &metrics) {
  // 1. Crosshair (centered)
  if (!metrics.isPaused) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
    ImGui::Begin("Crosshair", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoInputs);
    ImGui::Text("+");
    ImGui::End();
  }

  // 2. F3 Debug Screen
  if (metrics.showF3) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.3f);
    ImGui::Begin("Debug F3", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs);

    ImGui::Text("Minecraft Clone Engine (Raw Vulkan)");
    ImGui::Separator();
    ImGui::Text("FPS: %d", (int)metrics.fps);
    ImGui::Text("Frame Time: %.2f ms", metrics.frameTimeMs);
    ImGui::Text("TPS: %.1f", metrics.tps);
    ImGui::Text("Vertices Rendered: %u", metrics.verticesRendered);
    ImGui::Text("Chunks Loaded: %u", metrics.chunksLoaded);
    ImGui::Text("Chunks Rendered: %u", metrics.chunksRendered);
    ImGui::Text("RAM Usage: %.2f MB", metrics.ramUsageMB);
    ImGui::Text("CPU Usage: %.1f%%", metrics.cpuUsagePercent);

    if (!metrics.frameTimes.empty()) {
      ImGui::PlotLines("Frame Times", metrics.frameTimes.data(),
                       (int)metrics.frameTimes.size(),
                       (int)metrics.frameTimeIndex, nullptr, 0.0f, 33.3f,
                       ImVec2(300, 100));
    }
    ImGui::End();
  }

  // 3. Pause Menu
  if (metrics.isPaused && !metrics.showSettings) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Pause Menu", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Game Paused");
    ImGui::Separator();

    if (ImGui::Button("Settings", ImVec2(200, 40))) {
      metrics.showSettings = true;
    }
    ImGui::End();
  }

  // 4. Settings Menu
  if (metrics.isPaused && metrics.showSettings) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Settings", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Graphics Options");
    ImGui::Separator();

    ImGui::SliderInt("Render Distance", &metrics.viewDistance, 2, 64);
    ImGui::Checkbox("Enable CUDA Raytracing (3D DDA)", &metrics.enableRaytracing);

    ImGui::Dummy(ImVec2(0, 20)); // Spacer

    if (ImGui::Button("Close Settings", ImVec2(200, 40))) {
      metrics.showSettings = false;
    }
    ImGui::End();
  }

  // 5. Profiler
  if (metrics.showProfiler) {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine Profiler (F3+P)", &metrics.showProfiler);
    
    if (ImGui::BeginTable("ProfilerTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable)) {
      ImGui::TableSetupColumn("Scope Name");
      ImGui::TableSetupColumn("Time (ms)");
      ImGui::TableSetupColumn("Time (ns)");
      ImGui::TableSetupColumn("Thread Hash");
      ImGui::TableHeadersRow();

      const auto& results = mc::core::Profiler::Get().GetResults();
      for (const auto& result : results) {
        ImGui::TableNextRow();
        
        ImGui::TableNextColumn();
        ImGui::Text("%s", result.name);
        
        ImGui::TableNextColumn();
        ImGui::Text("%.4f", result.durationNs / 1000000.0);
        
        ImGui::TableNextColumn();
        ImGui::Text("%lld", result.durationNs);
        
        ImGui::TableNextColumn();
        std::hash<std::thread::id> hasher;
        ImGui::Text("%zu", hasher(result.threadId));
      }
      ImGui::EndTable();
    }
    
    ImGui::End();
  }
}

} // namespace mc::render
