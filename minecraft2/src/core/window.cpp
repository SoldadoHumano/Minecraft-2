#include "window.h"
#include <stdexcept>
#include <iostream>

namespace mc::core {

Window::Window(int width, int height, const std::string& title) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Tell GLFW not to create an OpenGL context, we are using Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);

    std::cout << "[Window] Created window (" << width << "x" << height << ")\n";
}

void Window::FramebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
}

Window::~Window() {
    std::cout << "[Window] Destroying window\n";
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::PollEvents() {
    glfwPollEvents();
}

} // namespace mc::core
