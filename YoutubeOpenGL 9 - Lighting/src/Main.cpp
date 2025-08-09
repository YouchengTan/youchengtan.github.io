//------- Ignore this ----------
#include <filesystem>
namespace fs = std::filesystem;
//------------------------------

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb/stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Texture.h"
#include "shaderClass.h"
#include "VAO.h"
#include "VBO.h"
#include "EBO.h"
#include "Jelly.h"
#include "Camera.h"

const unsigned int width = 800;
const unsigned int height = 800;

// Simple quad helper (pos, color, uv, normal) = 11 floats
struct QuadGeo {
    std::vector<GLfloat> v;
    std::vector<GLuint>  i;
    VAO vao; VBO* vbo = nullptr; EBO* ebo = nullptr;
    void build() {
        vao.Bind();
        vbo = new VBO(v.data(), (GLsizeiptr)(v.size() * sizeof(GLfloat)));
        ebo = new EBO(i.data(), (GLsizeiptr)(i.size() * sizeof(GLuint)));
        vao.LinkAttrib(*vbo, 0, 3, GL_FLOAT, 11 * sizeof(float), (void*)0);                 // pos
        vao.LinkAttrib(*vbo, 1, 3, GL_FLOAT, 11 * sizeof(float), (void*)(3 * sizeof(float))); // color
        vao.LinkAttrib(*vbo, 2, 2, GL_FLOAT, 11 * sizeof(float), (void*)(6 * sizeof(float))); // uv
        vao.LinkAttrib(*vbo, 3, 3, GL_FLOAT, 11 * sizeof(float), (void*)(8 * sizeof(float))); // normal
        vao.Unbind(); vbo->Unbind(); ebo->Unbind();
    }
    void draw() { vao.Bind(); glDrawElements(GL_TRIANGLES, (GLsizei)i.size(), GL_UNSIGNED_INT, 0); vao.Unbind(); }
};

int main() {
    // Init GLFW / context
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(width, height, "Jelly Cubes", NULL, NULL);
    if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    gladLoadGL();
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);

    // Shaders
    Shader shader("default.vert", "default.frag");   // used for everything textured/lit
    Shader lightShader("light.vert", "light.frag");  // small light cube

    // Camera
    Camera camera(width, height, glm::vec3(0.0f, 0.5f, 0.9f));

    // Light
    glm::vec4 lightColor = glm::vec4(1, 1, 1, 1);
    glm::vec3 lightPos = glm::vec3(0.8f, 1.0f, 0.8f);
    // Tiny light cube geo
    GLfloat lightVerts[] = { -0.05f,-0.05f, 0.05f, -0.05f,-0.05f,-0.05f, 0.05f,-0.05f,-0.05f, 0.05f,-0.05f, 0.05f,
                             -0.05f, 0.05f, 0.05f, -0.05f, 0.05f,-0.05f, 0.05f, 0.05f,-0.05f, 0.05f, 0.05f, 0.05f };
    GLuint  lightIdx[] = { 0,1,2, 0,2,3, 0,4,7, 0,7,3, 3,7,6, 3,6,2,
                             2,6,5, 2,5,1, 1,5,4, 1,4,0, 4,5,6, 4,6,7 };
    VAO lightVAO; VBO lightVBO(lightVerts, sizeof(lightVerts)); EBO lightEBO(lightIdx, sizeof(lightIdx));
    lightVAO.Bind();
    lightVBO.Bind();
    lightEBO.Bind();
    lightVAO.LinkAttrib(lightVBO, 0, 3, GL_FLOAT, 3 * sizeof(float), (void*)0);
    lightVAO.Unbind();
    lightVBO.Unbind();

    // Textures (both use sampler "tex0" at unit 0; we bind the one we need before drawing)
    std::string parentDir = (fs::current_path().fs::path::parent_path()).string();
    std::string texPath = "/Resources/";
    Texture brickTex((parentDir + texPath + "brick.png").c_str(), GL_TEXTURE_2D, GL_TEXTURE0, GL_RGBA, GL_UNSIGNED_BYTE);
    Texture jellyTex((parentDir + texPath + "slime.png").c_str(), GL_TEXTURE_2D, GL_TEXTURE0, GL_RGB, GL_UNSIGNED_BYTE);
    brickTex.texUnit(shader, "tex0", 0); // set once; we’ll bind brickTex or jellyTex on unit 0 before draw

    // Container (open top)
    Container box;
    box.min = glm::vec3(-1.0f, 0.0f, -1.0f);
    box.max = glm::vec3(+1.0f, 1.2f, +1.0f);
    box.restitution = 0.25f;
    box.friction = 0.6f;

    // Two jelly cubes — lighter mesh + gentle springs (PoC-friendly)
    Jelly j1(glm::vec3(0.00f, 0.70f, 0.00f), 0.35f, glm::vec3(0), glm::vec3(0), 0.05f, 0.25f, 2);
    Jelly j2(glm::vec3(0.22f, 0.95f, 0.00f), 0.35f, glm::vec3(0), glm::vec3(0), 0.05f, 0.25f, 2);


    // Build brick floor and 4 brick walls as world-space quads
    auto makeQuad = [](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec3 n,
        float uTiles, float vTiles) {
            QuadGeo q;
            glm::vec3 col(1.0f, 1.0f, 1.0f); // color is mostly unused by the fragment shader (texture dominates)
            // v0
            q.v.insert(q.v.end(), { p0.x,p0.y,p0.z,  col.x,col.y,col.z,  0.0f,      0.0f,      n.x,n.y,n.z });
            // v1
            q.v.insert(q.v.end(), { p1.x,p1.y,p1.z,  col.x,col.y,col.z,  uTiles,    0.0f,      n.x,n.y,n.z });
            // v2
            q.v.insert(q.v.end(), { p2.x,p2.y,p2.z,  col.x,col.y,col.z,  uTiles,    vTiles,    n.x,n.y,n.z });
            // v3
            q.v.insert(q.v.end(), { p3.x,p3.y,p3.z,  col.x,col.y,col.z,  0.0f,      vTiles,    n.x,n.y,n.z });
            q.i = { 0,1,2, 0,2,3 };
            q.build();
            return q;
        };

    const float tileU = 6.0f, tileV = 6.0f; // repeat bricks nicely

    // Floor (y = box.min.y), normal +Y
    QuadGeo floor = makeQuad(
        glm::vec3(box.min.x, box.min.y, box.max.z),
        glm::vec3(box.max.x, box.min.y, box.max.z),
        glm::vec3(box.max.x, box.min.y, box.min.z),
        glm::vec3(box.min.x, box.min.y, box.min.z),
        glm::vec3(0, 1, 0), tileU, tileV);

    // +X wall (right), normal pointing -X
    QuadGeo wallPosX = makeQuad(
        glm::vec3(box.max.x, box.min.y, box.min.z),
        glm::vec3(box.max.x, box.min.y, box.max.z),
        glm::vec3(box.max.x, box.max.y, box.max.z),
        glm::vec3(box.max.x, box.max.y, box.min.z),
        glm::vec3(-1, 0, 0), tileU, tileV);

    // -X wall (left), normal +X
    QuadGeo wallNegX = makeQuad(
        glm::vec3(box.min.x, box.min.y, box.max.z),
        glm::vec3(box.min.x, box.min.y, box.min.z),
        glm::vec3(box.min.x, box.max.y, box.min.z),
        glm::vec3(box.min.x, box.max.y, box.max.z),
        glm::vec3(1, 0, 0), tileU, tileV);

    // +Z wall (front), normal -Z
    QuadGeo wallPosZ = makeQuad(
        glm::vec3(box.min.x, box.min.y, box.max.z),
        glm::vec3(box.max.x, box.min.y, box.max.z),
        glm::vec3(box.max.x, box.max.y, box.max.z),
        glm::vec3(box.min.x, box.max.y, box.max.z),
        glm::vec3(0, 0, -1), tileU, tileV);

    // -Z wall (back), normal +Z
    QuadGeo wallNegZ = makeQuad(
        glm::vec3(box.max.x, box.min.y, box.min.z),
        glm::vec3(box.min.x, box.min.y, box.min.z),
        glm::vec3(box.min.x, box.max.y, box.min.z),
        glm::vec3(box.max.x, box.max.y, box.min.z),
        glm::vec3(0, 0, 1), tileU, tileV);

    // Set static uniforms
    glm::mat4 I(1.0f);
    lightShader.Activate();
    glm::mat4 lightModel = glm::translate(I, lightPos);
    glUniformMatrix4fv(glGetUniformLocation(lightShader.ID, "model"), 1, GL_FALSE, glm::value_ptr(lightModel));
    glUniform4f(glGetUniformLocation(lightShader.ID, "lightColor"), lightColor.x, lightColor.y, lightColor.z, lightColor.w);

    shader.Activate();
    glUniform4f(glGetUniformLocation(shader.ID, "lightColor"), lightColor.x, lightColor.y, lightColor.z, lightColor.w);
    glUniform3f(glGetUniformLocation(shader.ID, "lightPos"), lightPos.x, lightPos.y, lightPos.z);
    glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(I));

    // Fixed-timestep physics
    double prevTime = glfwGetTime();
    double accumulator = 0.0;
    const double fixedDt = 1.0 / 120.0;

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        camera.Inputs(window);
        camera.updateMatrix(45.0f, 0.1f, 100.0f);

        // Step physics
        double t = glfwGetTime();
        accumulator += (t - prevTime);
        prevTime = t;

        // fun: space bar to "punch" both jelly cubes
        // if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { j1.apply_punch(); j2.apply_punch(); }

        while (accumulator >= fixedDt) {
            j1.Update((float)fixedDt, box);
            j2.Update((float)fixedDt, box);
            j1.CollideWith(j2);
            accumulator -= fixedDt;
        }

        // Common per-frame uniforms
        shader.Activate();
        glUniform3f(glGetUniformLocation(shader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
        camera.Matrix(shader, "camMatrix");

        // Draw floor & walls with BRICK texture
        brickTex.Bind();                 // unit 0; shader uses sampler "tex0"
        glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(I));
        floor.draw();
        wallPosX.draw();
        wallNegX.draw();
        wallPosZ.draw();
        wallNegZ.draw();
        brickTex.Unbind();

        // Draw jellies with SLIME texture (same sampler/unit)
        jellyTex.Bind();
        j1.Render();
        j2.Render();
        jellyTex.Unbind();

        // Draw light cube
        lightShader.Activate();
        camera.Matrix(lightShader, "camMatrix");
        lightVAO.Bind();
        glDrawElements(GL_TRIANGLES, (GLsizei)(sizeof(lightIdx) / sizeof(GLuint)), GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    lightVAO.Delete(); lightVBO.Delete(); lightEBO.Delete();
    brickTex.Delete(); jellyTex.Delete();
    shader.Delete(); lightShader.Delete();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
