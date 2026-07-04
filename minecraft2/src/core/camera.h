#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace mc::core {

class Camera {
public:
  Camera(glm::vec3 position, float fov, float aspect, float nearPlane,
         float farPlane);

  void Update(GLFWwindow *window, float deltaTime);

  glm::vec3 GetPosition() const { return m_position; }
  glm::mat4 GetViewMatrix() const;
  glm::mat4 GetProjectionMatrix() const;

  void SetAspect(float aspect);

  void SetCursorCaptured(bool captured) {
    m_cursorCaptured = captured;
    if (captured)
      m_firstMouse = true;
  }

  bool IsCursorCaptured() const { return m_cursorCaptured; }

  void UpdateAspectRatio(float aspect);

private:
  void UpdateCameraVectors();

  glm::vec3 m_position;
  glm::vec3 m_front;
  glm::vec3 m_up;
  glm::vec3 m_right;
  glm::vec3 m_worldUp;

  float m_yaw;
  float m_pitch;

  float m_movementSpeed;
  float m_mouseSensitivity;
  float m_fov;
  float m_aspect;
  float m_near;
  float m_far;

  bool m_firstMouse;
  float m_lastX;
  float m_lastY;

  bool m_cursorCaptured = false;
};

} // namespace mc::core
