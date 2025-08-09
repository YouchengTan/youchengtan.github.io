#ifndef CAMERA_CLASS_H
#define CAMERA_CLASS_H

#include<glad/glad.h>
#include<GLFW/glfw3.h>
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include<glm/gtx/rotate_vector.hpp>
#include<glm/gtx/vector_angle.hpp>

#include"shaderClass.h"

class Camera
{
public:
    glm::vec3 Position;
    glm::vec3 Target = glm::vec3(0.0f); // orbit center
    glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 cameraMatrix = glm::mat4(1.0f);

    int width, height;

    // Orbit parameters
    float distance = 3.0f;
    float yaw = -90.0f;   // horizontal rotation
    float pitch = 20.0f;  // vertical rotation
    float sensitivity = 0.6f; // rotate speed
    float zoomSpeed = 0.1f;

    bool rotating = false;
    bool firstClick = true;
    double lastX, lastY;

    Camera(int width, int height, glm::vec3 target);

    void updateMatrix(float FOVdeg, float nearPlane, float farPlane);
    void Matrix(Shader& shader, const char* uniform);
    void Inputs(GLFWwindow* window);
};

#endif