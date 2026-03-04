#pragma once

#include <types.h>

#include <glm/glm.hpp>
#include <SDL3/SDL_events.h>

struct CameraParams {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 pos;
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;
};

enum class CameraProjection {
    Perspective,
    Orthographic
};

// Heavily inspired by https://github.com/StudioCherno/Hazel/blob/2024.1/Hazel/src/Hazel/Editor/EditorCamera.h
class Camera {
public:
    Camera(f32 fov, const glm::vec2& viewport, f32 near, f32 far, CameraProjection projection = CameraProjection::Perspective);

    void reset();
    void update();
    void handleEvent(const SDL_Event& event);

    void setProjection(CameraProjection projection);
    void setViewport(f32 width, f32 height);
    const glm::vec2& getViewport() const { return m_viewport; }

    const glm::vec3& getPosition() const { return m_position; }

    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }
    void setViewportHovered(bool hovered) { m_viewportHovered = hovered; }

    const glm::mat4& getView() const;
    const glm::mat4& getProj() const;

    f32 getFov() const { return m_fov; }
    void setFov(f32 fov) { m_fov = fov; m_projDirty = true; }

    f32 getAspect() const { return m_aspect; }
    void setAspect(f32 aspect) { m_aspect = aspect; m_projDirty = true; }

    f32 getNear() const { return m_near; }
    void setNear(f32 near) { m_near = near; m_projDirty = true; }

    f32 getFar() const { return m_far; }
    void setFar(f32 far) { m_far = far; m_projDirty = true; }

    CameraParams getParams() const;

private:
    void updateView();
    void updateProjection();

    glm::vec3 computePosition() const;
    glm::quat getOrientation() const;
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;

    void rotateCamera(const glm::vec2& delta);
    void panCamera(const glm::vec2& delta);
    void zoomCamera(f32 delta);

    glm::vec2 panSpeed() const;
    f32 zoomSpeed() const;

private:
    glm::mat4 m_view{};
    glm::mat4 m_proj{};

    glm::vec3 m_position{};
    glm::vec3 m_direction{};
    glm::vec3 m_target{};

    glm::vec3 m_positionDelta{};
    glm::vec2 m_lastMousePos{};

    f32 m_fov;
    f32 m_aspect;
    f32 m_near;
    f32 m_far;

    f32 m_pitch = 0.0f;
    f32 m_yaw = 1.0f;
    f32 m_distance = 10.0f;

    f32 m_pitchDelta = 0.0f;
    f32 m_yawDelta = 0.0f;

    f32 m_orthoScale = 10.0f;

    glm::vec2 m_viewport;
    CameraProjection m_projection = CameraProjection::Perspective;

    bool m_viewportHovered;
    bool m_active = false;
    bool m_projDirty = true;

    static constexpr f32 ROTATION_SPEED = 0.3f;
};
