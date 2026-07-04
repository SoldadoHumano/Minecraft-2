#include "camera.h"
#include <glm/gtc/matrix_transform.hpp>

namespace mc::core {

Camera::Camera(glm::vec3 position, float fov, float aspect, float nearPlane,
               float farPlane)
    : m_position(position), m_worldUp(glm::vec3(0.0f, 1.0f, 0.0f)),
      m_yaw(-90.0f), m_pitch(0.0f), m_movementSpeed(5.0f),
      m_mouseSensitivity(0.1f), m_fov(fov), m_aspect(aspect), m_near(nearPlane),
      m_far(farPlane), m_firstMouse(true), m_lastX(0.0f), m_lastY(0.0f) {
  UpdateCameraVectors();
}

void Camera::Update(GLFWwindow *window, float deltaTime) {
  // Keyboard Movement
  float velocity = m_movementSpeed * deltaTime;
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    m_position += m_front * velocity;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    m_position -= m_front * velocity;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    m_position -= m_right * velocity;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    m_position += m_right * velocity;
  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
    m_position += m_worldUp * velocity;
  if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    m_position -= m_worldUp * velocity;

  // Camera update now respects m_cursorCaptured flag set by main.cpp
  if (!m_cursorCaptured)
    return;

  // Mouse Look
  double xpos, ypos;
  glfwGetCursorPos(window, &xpos, &ypos);

  if (m_firstMouse) {
    m_lastX = static_cast<float>(xpos);
    m_lastY = static_cast<float>(ypos);
    m_firstMouse = false;
  }

  float xoffset = static_cast<float>(xpos) - m_lastX;
  float yoffset =
      m_lastY - static_cast<float>(
                    ypos); // reversed since y-coordinates go from bottom to top
  m_lastX = static_cast<float>(xpos);
  m_lastY = static_cast<float>(ypos);

  xoffset *= m_mouseSensitivity;
  yoffset *= m_mouseSensitivity;

  m_yaw += xoffset;
  m_pitch += yoffset;

  if (m_pitch > 89.0f)
    m_pitch = 89.0f;
  if (m_pitch < -89.0f)
    m_pitch = -89.0f;

  UpdateCameraVectors();
}

void Camera::UpdateAspectRatio(float aspect) {
  m_aspect = aspect;
  UpdateCameraVectors();
}

void Camera::UpdateCameraVectors() {
  glm::vec3 front;
  front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
  front.y = sin(glm::radians(m_pitch));
  front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
  m_front = glm::normalize(front);
  m_right = glm::normalize(glm::cross(m_front, m_worldUp));
  m_up = glm::normalize(glm::cross(m_right, m_front));
}

glm::mat4 Camera::GetViewMatrix() const {
  return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::GetProjectionMatrix() const {
  glm::mat4 proj =
      glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
  proj[1][1] *= -1; // Invert Y for Vulkan
  return proj;
}

void Camera::SetAspect(float aspect) { m_aspect = aspect; }

} // namespace mc::core
