#include "core/camera.h"
#include "core/job_system.h"
#include "core/metrics.h"
#include "core/profiler.h"
#include "core/window.h"
#include "logzilla/logzilla.h"
#include "render/renderer.h"
#include "world/chunk.h"
#include "world/chunk_manager.h"
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <chrono>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <volk.h>

#ifdef _WIN32
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#include <windows.h>
#include <psapi.h>

static ULONGLONG SubtractTimes(const FILETIME &a, const FILETIME &b) {
  LARGE_INTEGER la, lb;
  la.LowPart = a.dwLowDateTime;
  la.HighPart = a.dwHighDateTime;
  lb.LowPart = b.dwLowDateTime;
  lb.HighPart = b.dwHighDateTime;
  return la.QuadPart - lb.QuadPart;
}
#endif

int main() {
  logzilla_init("engine_run_logzilla.log");
  LOGZILLA_INFO("Minecraft Clone Engine Starting...");

  LOGZILLA_INFO("volkInitialize...");
  if (volkInitialize() != VK_SUCCESS) {
    LOGZILLA_FATAL(
        "Failed to initialize Volk! (Vulkan SDK/Drivers might be missing)");
    logzilla_shutdown();
    return -1;
  }
  LOGZILLA_INFO("volkInitialize OK.");

  LOGZILLA_INFO("JobSystem::Initialize...");
  mc::core::JobSystem::Initialize();
  LOGZILLA_INFO("JobSystem initialized.");

  try {
    LOGZILLA_INFO("Creating Window...");
    mc::core::Window window(1280, 720, "Minecraft Clone");
    LOGZILLA_INFO("Window created OK, nativeWindow=%p",
                  (void *)window.GetNativeWindow());

    LOGZILLA_INFO("Creating VulkanContext...");
    mc::render::VulkanContext vulkanContext(window.GetNativeWindow(),
                                            "MinecraftClone");
    LOGZILLA_INFO("VulkanContext created OK.");

    LOGZILLA_INFO("Creating Renderer...");
    mc::render::Renderer renderer(vulkanContext, 1280, 720);
    LOGZILLA_INFO("Renderer created OK.");

    LOGZILLA_INFO("Creating Camera...");
    mc::core::Camera camera(glm::vec3(0.0f, 150.0f, 0.0f), 60.0f,
                            1280.0f / 720.0f, 0.1f, 1000.0f);
    LOGZILLA_INFO("Camera created OK.");
    mc::core::EngineMetrics metrics;

    LOGZILLA_INFO("Creating ChunkManager...");
    mc::world::ChunkManager chunkManager(42); // Seed = 42
    LOGZILLA_INFO("ChunkManager created OK.");

    chunkManager.SetMeshReadyCallback(
        [&renderer](const mc::world::ChunkMesh &mesh) {
          renderer.UploadMeshAsync(mesh);
        });

    chunkManager.SetChunkUnloadCallback(
        [&renderer](int cx, int cz) { renderer.UnloadChunk(cx, cz); });
    LOGZILLA_INFO("Callbacks set. Entering game loop.");
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastTime = startTime;

    int frameCount = 0;
    float fpsTimer = 0.0f;

    while (!window.ShouldClose()) {
      mc::core::Profiler::Get().BeginFrame();
      PROFILE_SCOPE("Main Frame");

      window.PollEvents();

      auto currentTime = std::chrono::high_resolution_clock::now();
      float time = std::chrono::duration<float, std::chrono::seconds::period>(
                       currentTime - startTime)
                       .count();
      float deltaTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              currentTime - lastTime)
              .count();
      lastTime = currentTime;

      // Escape Menu Logic
      static bool escWasPressed = false;
      bool escIsPressed =
          (glfwGetKey(window.GetNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS);
      if (escIsPressed && !escWasPressed) {
        metrics.isPaused = !metrics.isPaused;
        if (metrics.isPaused) {
          glfwSetInputMode(window.GetNativeWindow(), GLFW_CURSOR,
                           GLFW_CURSOR_NORMAL);
          camera.SetCursorCaptured(false);
        } else {
          glfwSetInputMode(window.GetNativeWindow(), GLFW_CURSOR,
                           GLFW_CURSOR_DISABLED);
          camera.SetCursorCaptured(true);
        }
      }
      escWasPressed = escIsPressed;

      // ImGui New Frame
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      // F3 Toggle Logic
      static bool f3WasPressed = false;
      static bool pWasPressed = false;
      bool f3IsPressed =
          (glfwGetKey(window.GetNativeWindow(), GLFW_KEY_F3) == GLFW_PRESS);
      bool pIsPressed =
          (glfwGetKey(window.GetNativeWindow(), GLFW_KEY_P) == GLFW_PRESS);

      if (f3IsPressed && pIsPressed && !pWasPressed) {
        metrics.showProfiler = !metrics.showProfiler;
      } else if (f3IsPressed && !f3WasPressed && !pIsPressed) {
        metrics.showF3 = !metrics.showF3;
      }
      f3WasPressed = f3IsPressed;
      pWasPressed = pIsPressed;

      // Chunk Loading Updates
      static int previousViewDistance = metrics.viewDistance;

      if (!metrics.isPaused) {
        if (metrics.viewDistance < previousViewDistance) {
          chunkManager.Clear();
        }
        previousViewDistance = metrics.viewDistance;

        int playerChunkX =
            static_cast<int>(std::floor(camera.GetPosition().x / 16.0f));
        int playerChunkZ =
            static_cast<int>(std::floor(camera.GetPosition().z / 16.0f));
        chunkManager.Update(playerChunkX, playerChunkZ, metrics.viewDistance);
      }

      // Metrics calculation
      frameCount++;
      fpsTimer += deltaTime;
      if (fpsTimer >= 1.0f) {
        metrics.fps = (float)frameCount / fpsTimer;
        frameCount = 0;
        fpsTimer = 0.0f;
      }
      metrics.frameTimeMs = deltaTime * 1000.0f;
      metrics.tps = 20.0f; // Placeholder until fixed timestep tick is added

      metrics.frameTimes[metrics.frameTimeIndex] = metrics.frameTimeMs;
      metrics.frameTimeIndex =
          (metrics.frameTimeIndex + 1) % metrics.frameTimes.size();

      // OS Metrics update every 500ms
      static float osMetricTimer = 0.0f;
      osMetricTimer += deltaTime;
      if (osMetricTimer >= 0.5f) {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
          metrics.ramUsageMB = (float)pmc.WorkingSetSize / (1024.0f * 1024.0f);
        }

        static FILETIME prevSysKernel, prevSysUser;
        static FILETIME prevProcKernel, prevProcUser;
        static bool firstCpuCall = true;

        FILETIME sysIdle, sysKernel, sysUser;
        FILETIME procCreation, procExit, procKernel, procUser;

        if (GetSystemTimes(&sysIdle, &sysKernel, &sysUser) &&
            GetProcessTimes(GetCurrentProcess(), &procCreation, &procExit,
                            &procKernel, &procUser)) {

          if (!firstCpuCall) {
            ULONGLONG sysDiff = SubtractTimes(sysKernel, prevSysKernel) +
                                SubtractTimes(sysUser, prevSysUser);
            ULONGLONG procDiff = SubtractTimes(procKernel, prevProcKernel) +
                                 SubtractTimes(procUser, prevProcUser);

            if (sysDiff > 0) {
              metrics.cpuUsagePercent = (float)((procDiff * 100.0) / sysDiff);
            }
          }

          prevSysKernel = sysKernel;
          prevSysUser = sysUser;
          prevProcKernel = procKernel;
          prevProcUser = procUser;
          firstCpuCall = false;
        }
#endif
        osMetricTimer = 0.0f;
      }

      if (window.WasWindowResized()) {
        int width, height;
        glfwGetFramebufferSize(window.GetNativeWindow(), &width, &height);
        while (width == 0 || height == 0) {
          glfwGetFramebufferSize(window.GetNativeWindow(), &width, &height);
          glfwWaitEvents();
        }
        renderer.RecreateSwapchain(width, height);
        camera.UpdateAspectRatio((float)width / (float)height);
        window.ResetWindowResizedFlag();
        continue;
      }

      camera.Update(window.GetNativeWindow(), deltaTime);
      if (!renderer.DrawFrame(camera, time, metrics, chunkManager)) {
        window.ResetWindowResizedFlag(); // force a recreate next frame

        int width, height;
        glfwGetFramebufferSize(window.GetNativeWindow(), &width, &height);
        renderer.RecreateSwapchain(width, height);
        camera.UpdateAspectRatio((float)width / (float)height);
      }
      mc::core::Profiler::Get().EndFrame();
    }

    renderer.WaitIdle();
  } catch (const std::exception &e) {
    LOGZILLA_FATAL("Fatal Error: %s", e.what());
  }

  mc::core::JobSystem::Shutdown();

  LOGZILLA_INFO("Engine Shutdown Cleanly.");
  logzilla_shutdown();

  return 0;
}
