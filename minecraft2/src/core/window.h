#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

namespace mc::core {

class Window {
public:
  Window(int width, int height, const std::string &title);
  ~Window();

  // Prevent copying
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;

  bool ShouldClose() const;
  void PollEvents();

  GLFWwindow *GetNativeWindow() const { return m_window; }

  bool WasWindowResized() const { return m_framebufferResized; }
  void ResetWindowResizedFlag() { m_framebufferResized = false; }

private:
  static void FramebufferResizeCallback(GLFWwindow *window, int width,
                                        int height);

  GLFWwindow *m_window;
  bool m_framebufferResized = false;
};

} // namespace mc::core
