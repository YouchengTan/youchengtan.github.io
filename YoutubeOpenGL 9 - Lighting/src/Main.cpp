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
#include <glm/gtc/matrix_inverse.hpp> // 3D drag
#include <limits>
#include <glm/gtc/constants.hpp>
#include <cmath>




#include "Texture.h"
#include "shaderClass.h"
#include "VAO.h"
#include "VBO.h"
#include "EBO.h"
#include "Jelly.h"
#include "Camera.h"
#include <algorithm>

const unsigned int width = 800;
const unsigned int height = 800;
double lastMouseX, lastMouseY;

//3D drag globals
bool dragging = false, wasDown = false;
float grabDist = 0.0f;
glm::vec3 grabOffset(0.0f);
glm::vec3 dragTargetCenter(0.0f);
Jelly* draggedJelly = nullptr;  // pointer to currently dragged jelly



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

// helpers for 3D drag
struct Ray { glm::vec3 o, d; };


// helpers for 3D drag
static Ray mouseRay(double mx, double my, int fbW, int fbH, const Camera& cam) {
    float x =  2.0f * float(mx) / float(fbW) - 1.0f;
    float y =  1.0f - 2.0f * float(my) / float(fbH);
    glm::vec4 pn(x, y, -1.0f, 1.0f);
    glm::vec4 pf(x, y,  1.0f, 1.0f);

    glm::mat4 inv = glm::inverse(cam.cameraMatrix);
    glm::vec4 wn = inv * pn; wn /= wn.w;
    glm::vec4 wf = inv * pf; wf /= wf.w;

    Ray r;
    r.o = glm::vec3(wn);
    r.d = glm::normalize(glm::vec3(wf - wn));
    return r;
}

// drag
static bool rayAABB(const Ray& r, const glm::vec3& bmin, const glm::vec3& bmax, float& tHit) {
    glm::vec3 t1 = (bmin - r.o) / r.d;
    glm::vec3 t2 = (bmax - r.o) / r.d;
    glm::vec3 tmin = glm::min(t1, t2);
    glm::vec3 tmax = glm::max(t1, t2);
    float t0 = std::max(std::max(tmin.x, tmin.y), tmin.z);
    float t1f = std::min(std::min(tmax.x, tmax.y), tmax.z);
    if (t1f >= t0 && t1f > 0.0f) { tHit = (t0 > 0.0f) ? t0 : t1f; return true; }
    return false;
}

struct SphereGeo {
    std::vector<GLfloat> v;
    std::vector<GLuint>  i;
    VAO vao; VBO* vbo = nullptr; EBO* ebo = nullptr;

    void build(int stacks = 24, int slices = 36) {
        v.clear(); i.clear(); v.reserve((stacks+1)*(slices+1)*11);
        for (int y = 0; y <= stacks; ++y) {
            float v01  = (float)y / (float)stacks;
            float phi  = v01 * glm::pi<float>();
            float cph  = std::cos(phi), sph = std::sin(phi);
            for (int x = 0; x <= slices; ++x) {
                float u01   = (float)x / (float)slices;
                float theta = u01 * glm::two_pi<float>();
                float cth   = std::cos(theta), sth = std::sin(theta);
                glm::vec3 n = glm::vec3(cth * sph, cph, sth * sph);   // unit normal
                glm::vec3 p = n;                                      // unit sphere
                float u = u01, vtex = 1.0f - v01;
                glm::vec3 col(1.0f); 
                v.insert(v.end(), { p.x,p.y,p.z,  n.x,n.y,n.z,  u,vtex,  col.x,col.y,col.z });
            }
        }
        int row = slices + 1;
        for (int y = 0; y < stacks; ++y) {
            for (int x = 0; x < slices; ++x) {
                GLuint i0 = y * row + x;
                GLuint i1 = i0 + 1;
                GLuint i2 = i0 + row + 1;
                GLuint i3 = i0 + row;
                i.insert(i.end(), { i0,i1,i2,  i0,i2,i3 });
            }
        }
        vao.Bind();
        vbo = new VBO(v.data(), (GLsizeiptr)(v.size() * sizeof(GLfloat)));
        ebo = new EBO(i.data(), (GLsizeiptr)(i.size() * sizeof(GLuint)));
        vao.LinkAttrib(*vbo, 0, 3, GL_FLOAT, 11 * sizeof(float), (void*)0);                 // pos
        vao.LinkAttrib(*vbo, 1, 3, GL_FLOAT, 11 * sizeof(float), (void*)(3 * sizeof(float)));// normal
        vao.LinkAttrib(*vbo, 2, 2, GL_FLOAT, 11 * sizeof(float), (void*)(6 * sizeof(float)));// uv
        vao.LinkAttrib(*vbo, 3, 3, GL_FLOAT, 11 * sizeof(float), (void*)(8 * sizeof(float)));// color
        vao.Unbind(); if (vbo) vbo->Unbind(); if (ebo) ebo->Unbind();
    }
    void draw() { vao.Bind(); glDrawElements(GL_TRIANGLES, (GLsizei)i.size(), GL_UNSIGNED_INT, 0); vao.Unbind(); }
};

struct Body {
    float radius;        
    float orbit;         
    float speed;         
    glm::vec3 color;     
    float phase;         
};

// model matrix
static inline glm::mat4 TRS(const glm::vec3& t, float s) {
    return glm::scale(glm::translate(glm::mat4(1.0f), t), glm::vec3(s));
}


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
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


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
    // jellycat
    Texture brickTex((parentDir + texPath + "brick.png").c_str(),
                    GL_TEXTURE_2D, GL_TEXTURE0, GL_RGBA, GL_UNSIGNED_BYTE);
    Texture catFace((parentDir + texPath + "cat.png").c_str(),     GL_TEXTURE_2D, GL_TEXTURE0, GL_RGBA, GL_UNSIGNED_BYTE, 0.95f);
    Texture catFur((parentDir + texPath + "catfur.png").c_str(),   GL_TEXTURE_2D, GL_TEXTURE0, GL_RGBA, GL_UNSIGNED_BYTE, 1.0f);


    brickTex.texUnit(shader, "tex0", 0);
    // Stop wrap/bleed on the cat textures
    catFace.Bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // no mip bleed
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    catFace.Unbind();

    catFur.Bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // no mip bleed
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    catFur.Unbind();
    GLuint whiteTex = 0;
    {
        glGenTextures(1, &whiteTex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        unsigned int px = 0xFFFFFFFF; // RGBA white
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    //solar cat
    SphereGeo sphere;
    sphere.build(24, 36);
    glm::vec3 solarCenter(0.0f, 1.45f, 0.0f);
    Body sun { 0.12f, 0.00f, 0.00f, glm::vec3(1.00f, 0.95f, 0.30f), 0.0f };
    std::vector<Body> planets = {
        { 0.045f, 0.36f, 1.20f, glm::vec3(0.70f, 0.70f, 0.85f), 0.00f },
        { 0.055f, 0.54f, 0.80f, glm::vec3(0.95f, 0.55f, 0.20f), 0.60f },
        { 0.065f, 0.78f, 0.55f, glm::vec3(0.25f, 0.65f, 1.00f), 1.10f },
    };

    Container box;
    box.min = glm::vec3(-2.0f, 0.0f, -2.0f);
    box.max = glm::vec3(+2.0f, 2.4f, +2.0f);



    box.restitution = 0.25f;
    box.friction = 0.6f;

    // Two jelly cubes � lighter mesh + gentle springs (PoC-friendly)
    Jelly j1(glm::vec3(0.00f, 0.70f, 0.00f), 0.35f, glm::vec3(0), glm::vec3(0), 0.10f, 0.020f, 4);
    Jelly j2(glm::vec3(0.22f, 0.95f, 0.00f), 0.35f, glm::vec3(0), glm::vec3(0), 0.10f, 0.00f, 4);


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

    const float tileU = 100.0f, tileV = 100.0f; // repeat bricks nicely

    // Floor (y = box.min.y), normal +Y
    QuadGeo floor = makeQuad(
    glm::vec3(-50.0f, 0.0f, -50.0f),
    glm::vec3( 50.0f, 0.0f, -50.0f),
    glm::vec3( 50.0f, 0.0f,  50.0f),
    glm::vec3(-50.0f, 0.0f,  50.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    tileU, tileV);

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

    glUniform1f(glGetUniformLocation(shader.ID, "jellyWrap"),      0.4f);
    glUniform1f(glGetUniformLocation(shader.ID, "jellySpecular"),  0.7f);
    glUniform1f(glGetUniformLocation(shader.ID, "jellyShininess"), 64.0f);
    glUniform4f(glGetUniformLocation(shader.ID, "jellyTint"), 1.0f, 1.0f, 1.0f, 1.0f); // match the alpha above

    // Fixed-timestep physics
    double prevTime = glfwGetTime();
    double accumulator = 0.0;
    const double fixedDt = 1.0 / 120.0;

    while (!glfwWindowShouldClose(window)) { // render loo entrance
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!dragging) camera.Inputs(window);
        camera.updateMatrix(45.0f, 0.1f, 100.0f);
        // LMB press/release detection
        bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        if (down && !wasDown) {
            double mx, my; glfwGetCursorPos(window, &mx, &my);
            int winW, winH; glfwGetWindowSize(window, &winW, &winH);
            double sx = double(fbWidth)  / double(winW);
            double sy = double(fbHeight) / double(winH);

            Ray r = mouseRay(mx * sx, my * sy, fbWidth, fbHeight, camera);

            // Check all jellies and pick closest
            float closestHit = std::numeric_limits<float>::max();
            draggedJelly = nullptr;

            // Check j1
            float tHit;
            if (rayAABB(r, j1.getMin(), j1.getMax(), tHit)) {
                if (tHit < closestHit) {
                    closestHit = tHit;
                    draggedJelly = &j1;
                }
            }

            // Check j2
            if (rayAABB(r, j2.getMin(), j2.getMax(), tHit)) {
                if (tHit < closestHit) {
                    closestHit = tHit;
                    draggedJelly = &j2;
                }
            }

            if (draggedJelly) {
                dragging = true;
                grabDist = closestHit;
                glm::vec3 hit = r.o + r.d * closestHit;
                grabOffset = hit - draggedJelly->center;
                dragTargetCenter = draggedJelly->center;
            }
        }


        if (!down && wasDown) {
            dragging = false;
            draggedJelly = nullptr;  // Clear the dragged jelly reference
        }
        wasDown = down;

        if (dragging && draggedJelly) {
            double mx, my; 
            glfwGetCursorPos(window, &mx, &my);

            int winW, winH; 
            glfwGetWindowSize(window, &winW, &winH);
            double sx = double(fbWidth)  / double(winW);
            double sy = double(fbHeight) / double(winH);

            Ray r = mouseRay(mx * sx, my * sy, fbWidth, fbHeight, camera);
            glm::vec3 desired = r.o + r.d * grabDist;
            dragTargetCenter = desired - grabOffset;
        }



        // Step physics
        double t = glfwGetTime();
        accumulator += (t - prevTime);
        prevTime = t;

        while (accumulator >= fixedDt) {
            j1.Update((float)fixedDt, box);
            j2.Update((float)fixedDt, box);
            j1.CollideWith(j2);
            if (dragging && draggedJelly) {
                glm::vec3 smoothed = glm::mix(draggedJelly->center, dragTargetCenter, 0.35f);
                draggedJelly->TeleportToCenter(smoothed);
            }            
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
        // wallPosX.draw(); testing infinite floor effect
        // wallNegX.draw();
        // wallPosZ.draw();
        // wallNegZ.draw();
        brickTex.Unbind();

        // Draw jellies with SLIME texture (same sampler/unit)
        glDisable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_BLEND);
        catFace.Bind();
        j1.RenderFace(0);
        j2.RenderFace(0);
        catFace.Unbind();
        catFur.Bind();
        for (int f = 1; f < 6; ++f) {
            j1.RenderFace(f);
            j2.RenderFace(f);
        }
        catFur.Unbind();
        glEnable(GL_BLEND);

        //solar
// --- solar ---
shader.Activate();
camera.Matrix(shader, "camMatrix");

// bind WHITE texture so tint is pure color (no bricks)
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, whiteTex);

// make them a bit emissive-looking: kill spec for this pass
GLint specLoc = glGetUniformLocation(shader.ID, "jellySpecular");
GLint wrapLoc = glGetUniformLocation(shader.ID, "jellyWrap");
GLint modelLoc = glGetUniformLocation(shader.ID, "model");
GLint tintLoc  = glGetUniformLocation(shader.ID, "jellyTint");

// save old values by just restoring after (we know defaults used below)
glUniform1f(specLoc, 0.0f);
glUniform1f(wrapLoc, 0.0f);

// Sun (solid)
    {
        glm::mat4 M = TRS(solarCenter, sun.radius);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(M));
        glUniform4f(tintLoc, sun.color.r, sun.color.g, sun.color.b, 1.0f);
        sphere.draw();

        // Sun halo (additive)
        glBlendFunc(GL_ONE, GL_ONE);
        glm::mat4 Mh = TRS(solarCenter, sun.radius * 1.8f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mh));
        glUniform4f(tintLoc, sun.color.r, sun.color.g, sun.color.b, 0.12f);
        sphere.draw();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Planets
    double tsec = glfwGetTime();
    for (const Body& b : planets) {
        float ang = b.phase + (float)tsec * b.speed;
        glm::vec3 p = solarCenter + glm::vec3(std::cos(ang) * b.orbit, 0.0f, std::sin(ang) * b.orbit);

        // solid pass
        glm::mat4 M = TRS(p, b.radius);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(M));
        glUniform4f(tintLoc, b.color.r, b.color.g, b.color.b, 1.0f);
        sphere.draw();

        // glow pass
        glBlendFunc(GL_ONE, GL_ONE);
        glm::mat4 Mh = TRS(p, b.radius * 1.35f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mh));
        glUniform4f(tintLoc, b.color.r, b.color.g, b.color.b, 0.08f);
        sphere.draw();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // restore shader params for the rest of the scene
    glUniform1f(specLoc, 0.7f);
    glUniform1f(wrapLoc, 0.4f);
    glUniform4f(tintLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, 0);

        //end solar




        // Draw light cube
        lightShader.Activate();
        camera.Matrix(lightShader, "camMatrix");
        lightVAO.Bind();
        glDrawElements(GL_TRIANGLES, (GLsizei)(sizeof(lightIdx) / sizeof(GLuint)), GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteTextures(1, &whiteTex);
    lightVAO.Delete(); lightVBO.Delete(); lightEBO.Delete();
    brickTex.Delete(); catFace.Delete(); catFur.Delete();
    shader.Delete(); lightShader.Delete();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
