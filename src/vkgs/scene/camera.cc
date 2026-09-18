#include <vkgs/scene/camera.h>

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace vkgs {

Camera::Camera() {}

Camera::~Camera() {}

void Camera::SetWindowSize(uint32_t width, uint32_t height) {
  width_ = width;
  height_ = height;
}

void Camera::SetFov(float fov) {
  // dolly zoom
  r_ *= std::tan(fovy_ / 2.f) / std::tan(fov / 2.f);

  fovy_ = fov;
}

glm::mat4 Camera::ProjectionMatrix() const {
  float aspect = static_cast<float>(width_) / height_;
  glm::mat4 projection = glm::perspective(fovy_, aspect, near_, far_);

  // gl to vulkan projection matrix
  glm::mat4 conversion = glm::mat4(1.f);
  conversion[1][1] = -1.f;
  conversion[2][2] = 0.5f;
  conversion[3][2] = 0.5f;
  return conversion * projection;
}

glm::mat4 Camera::ViewMatrix() const {
  const glm::vec3 up = z_up_ ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
  return glm::lookAt(Eye(), center_, up);
}

glm::vec3 Camera::Eye() const {
  const auto sin_phi = std::sin(phi_);
  const auto cos_phi = std::cos(phi_);
  const auto sin_theta = std::sin(theta_);
  const auto cos_theta = std::cos(theta_);
  if (z_up_) {
    // camera = center + r (sin phi sin theta, sin phi cos theta, cos phi)
    return center_ + r_ * glm::vec3(sin_phi * sin_theta, sin_phi * cos_theta, cos_phi);
  }
  return center_ + r_ * glm::vec3(sin_phi * sin_theta, cos_phi, sin_phi * cos_theta);
}

void Camera::Rotate(float x, float y) {
  theta_ -= rotation_sensitivity_ * x;
  float eps = glm::radians(0.1f);
  phi_ = std::clamp(phi_ - rotation_sensitivity_ * y, eps, glm::pi<float>() - eps);
}

void Camera::Translate(float x, float y, float z) {
  const auto sin_phi = std::sin(phi_);
  const auto cos_phi = std::cos(phi_);
  const auto sin_theta = std::sin(theta_);
  const auto cos_theta = std::cos(theta_);
  glm::vec3 right, screen_up, forward;
  if (z_up_) {
    // camera = center + r (sin phi sin theta, sin phi cos theta, cos phi)
    right = glm::vec3(cos_theta, -sin_theta, 0.f);
    screen_up = glm::vec3(-cos_phi * sin_theta, -cos_phi * cos_theta, sin_phi);
    forward = glm::vec3(sin_phi * sin_theta, sin_phi * cos_theta, cos_phi);
  } else {
    // camera = center + r (sin phi sin theta, cos phi, sin phi cos theta)
    right = glm::vec3(cos_theta, 0.f, -sin_theta);
    screen_up = glm::vec3(-cos_phi * sin_theta, sin_phi, -cos_phi * cos_theta);
    forward = glm::vec3(sin_phi * sin_theta, cos_phi, sin_phi * cos_theta);
  }
  center_ += translation_sensitivity_ * r_ * (-x * right + y * screen_up + -z * forward);
}

void Camera::Zoom(float x) { r_ /= std::exp(zoom_sensitivity_ * x); }

void Camera::Dolly(float amount) {
  const auto sin_phi = std::sin(phi_);
  const auto cos_phi = std::cos(phi_);
  const auto sin_theta = std::sin(theta_);
  const auto cos_theta = std::cos(theta_);
  glm::vec3 forward;
  if (z_up_) {
    forward = glm::vec3(sin_phi * sin_theta, sin_phi * cos_theta, cos_phi);
  } else {
    forward = glm::vec3(sin_phi * sin_theta, cos_phi, sin_phi * cos_theta);
  }
  // forward points from center to camera; view direction is -forward.
  // Move center along -forward to fly into the scene (fixed speed, not scaled by r_).
  center_ += -amount * dolly_sensitivity_ * forward;
}

void Camera::DollyZoom(float scroll) {
  float new_fov = std::clamp(fovy_ - scroll * dolly_zoom_sensitivity_, min_fov(), max_fov());
  SetFov(new_fov);
}

}  // namespace vkgs
