#include <iostream>
#include <volk.h>
#include "core/window.h"
#include "core/job_system.h"
#include "core/camera.h"
#include "core/metrics.h"
#include "world/chunk.h"
#include "world/chunk_manager.h"
#include "render/renderer.h"
#include <chrono>
#include <fstream>

#ifdef _WIN32
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

int main() {
    std::ofstream logFile("engine_run.log");
    std::streambuf* originalCout = std::cout.rdbuf(logFile.rdbuf());
    std::streambuf* originalCerr = std::cerr.rdbuf(logFile.rdbuf());

    std::cout << "Minecraft Clone Engine Starting...\n";

    if (volkInitialize() != VK_SUCCESS) {
        std::cerr << "Failed to initialize Volk! (Vulkan SDK/Drivers might be missing)\n";
        return -1;
    }

    mc::core::JobSystem::Initialize();

    try {
        mc::core::Window window(1280, 720, "Minecraft Clone (Raw Vulkan)");
        
        mc::render::VulkanContext vulkanContext(window.GetNativeWindow(), "MinecraftClone");
        mc::render::Renderer renderer(vulkanContext, 1280, 720);

        mc::core::Camera camera(glm::vec3(8.0f, 70.0f, 8.0f), 45.0f, 1280.0f / 720.0f, 0.1f, 3000.0f);
        mc::core::EngineMetrics metrics;

        mc::world::ChunkManager chunkManager(42); // Seed = 42

        chunkManager.SetMeshReadyCallback([&renderer](const mc::world::ChunkMesh& mesh) {
            renderer.UploadMeshAsync(mesh);
        });

        chunkManager.SetChunkUnloadCallback([&renderer](int cx, int cz) {
            renderer.UnloadChunk(cx, cz);
        });
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastTime = startTime;
        
        int frameCount = 0;
        float fpsTimer = 0.0f;

        while (!window.ShouldClose()) {
            window.PollEvents();

            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
            float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Escape Menu Logic
            static bool escWasPressed = false;
            bool escIsPressed = (glfwGetKey(window.GetNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS);
            if (escIsPressed && !escWasPressed) {
                metrics.isPaused = !metrics.isPaused;
                if (metrics.isPaused) {
                    glfwSetInputMode(window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    camera.SetCursorCaptured(false);
                } else {
                    glfwSetInputMode(window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    camera.SetCursorCaptured(true);
                }
            }
            escWasPressed = escIsPressed;

            // UI Click Logic
            static bool mouseWasPressed = false;
            bool mouseIsPressed = (glfwGetMouseButton(window.GetNativeWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
            if (metrics.isPaused && mouseIsPressed) {
                double mx, my;
                glfwGetCursorPos(window.GetNativeWindow(), &mx, &my);
                if (!mouseWasPressed) {
                    renderer.HandleClick(mx, my, metrics);
                } else {
                    renderer.HandleDrag(mx, my, metrics);
                }
            }
            mouseWasPressed = mouseIsPressed;

            // F3 Toggle Logic
            static bool f3WasPressed = false;
            bool f3IsPressed = (glfwGetKey(window.GetNativeWindow(), GLFW_KEY_F3) == GLFW_PRESS);
            if (f3IsPressed && !f3WasPressed) {
                metrics.showF3 = !metrics.showF3;
            }
            f3WasPressed = f3IsPressed;

            // Chunk Loading Updates
            int playerChunkX = static_cast<int>(std::floor(camera.GetPosition().x / 16.0f));
            int playerChunkZ = static_cast<int>(std::floor(camera.GetPosition().z / 16.0f));
            chunkManager.Update(playerChunkX, playerChunkZ, metrics.viewDistance);

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
            if (!renderer.DrawFrame(camera, time, metrics)) {
                window.ResetWindowResizedFlag(); // force a recreate next frame
                int width, height;
                glfwGetFramebufferSize(window.GetNativeWindow(), &width, &height);
                renderer.RecreateSwapchain(width, height);
                camera.UpdateAspectRatio((float)width / (float)height);
            }
        }

        renderer.WaitIdle();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << "\n";
    }

    mc::core::JobSystem::Shutdown();

    std::cout << "Engine Shutdown Cleanly.\n";
    
    std::cout.rdbuf(originalCout);
    std::cerr.rdbuf(originalCerr);
    
    return 0;
}
