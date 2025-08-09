#include "Camera.h"

Camera::Camera(int width, int height, glm::vec3 target)
{
    Camera::width = width;
    Camera::height = height;
    Target = target;
}

void Camera::updateMatrix(float FOVdeg, float nearPlane, float farPlane)
{
    glm::vec3 offset;
    offset.x = distance * cos(glm::radians(pitch)) * cos(glm::radians(yaw));
    offset.y = distance * sin(glm::radians(pitch));
    offset.z = distance * cos(glm::radians(pitch)) * sin(glm::radians(yaw));

    Position = Target + offset;

    glm::mat4 view = glm::lookAt(Position, Target, Up);
    glm::mat4 projection = glm::perspective(glm::radians(FOVdeg), (float)width / height, nearPlane, farPlane);

    cameraMatrix = projection * view;
}


void Camera::Matrix(Shader& shader, const char* uniform)
{
    glUniformMatrix4fv(glGetUniformLocation(shader.ID, uniform), 1, GL_FALSE, glm::value_ptr(cameraMatrix));
}

// void Camera::Inputs(GLFWwindow* window)
// {
//     if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
//     {
//         double xpos, ypos;
//         glfwGetCursorPos(window, &xpos, &ypos);

//         if (firstClick)
//         {
//             lastX = xpos;
//             lastY = ypos;
//             firstClick = false;
//         }

//         float xoffset = xpos - lastX;
//         float yoffset = lastY - ypos;
//         lastX = xpos;
//         lastY = ypos;

//         yaw += xoffset * sensitivity;
//         pitch += yoffset * sensitivity;

//         if (pitch > 89.0f) pitch = 89.0f;
//         if (pitch < -89.0f) pitch = -89.0f;
//     }
//     else
//     {
//         firstClick = true;
//     }

//     double scrollY = 0.0;
//     if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) scrollY = -1.0;
//     if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) scrollY = 1.0;

//     distance += scrollY * zoomSpeed;
//     if (distance < 0.5f) distance = 0.5f;
// }
void Camera::Inputs(GLFWwindow* window)
{
    float rot = sensitivity;

    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) yaw   -= rot;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) yaw   += rot;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) pitch += rot;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) pitch -= rot;

    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS)
        distance -= zoomSpeed;
    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
        distance += zoomSpeed;

    if (distance < 0.5f) distance = 0.5f;
}

