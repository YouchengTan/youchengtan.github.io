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
#include <random>
#include <glm/common.hpp>



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

static inline glm::mat4 makePlanarShadow(const glm::vec4& plane, const glm::vec4& light)
{
    float dot = glm::dot(plane, light);
    glm::mat4 S(0.0f);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float I = (i == j) ? 1.0f : 0.0f;
            S[j][i] = dot * I - light[i] * plane[j];
        }
    }
    return S;
}


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

// heled functino of random firefly color
static glm::vec3 hsv2rgb(float h, float s, float v) {
    h = glm::fract(h) * 6.0f;
    int i = (int)std::floor(h);
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default:return {v, p, q};
    }
}

// visuals no physics
struct Pebble { glm::vec3 pos; float r; glm::vec3 color; };
struct Firefly { glm::vec3 pos; float r; float phase; glm::vec3 color; };
std::vector<Pebble> pebbles;
std::vector<Firefly> fireflies;
// shockwave
struct Ring { glm::vec3 p; float t; float seed; };
std::vector<Ring> rings;
struct Ember { glm::vec3 p, v; float t, life, r; };
std::vector<Ember> embers;
//more emberry effect
struct Scorch { glm::vec3 p; float t, life, r; float rot; };
std::vector<Scorch> scorches;
struct Smoke { glm::vec3 p, v; float t, life, r; };
std::vector<Smoke> smokes;

float timeScale   = 1.0f;
float slowmoTimer = 0.0f;
float shakeTimer  = 0.0f;

//meteor jelly cats start
struct Meteor {
    Jelly j;
    glm::vec4 tint;
    glm::vec3 lastCenter;
    float stillTime = 0.0f;
    //shockwave
    bool impacted = false;
    Meteor(const Jelly& jj, const glm::vec4& tt) : j(jj), tint(tt), lastCenter(jj.center) {}
};

std::vector<Meteor> meteors;
// meteor jelly cats end

// model matrix
static inline glm::mat4 TRS(const glm::vec3& t, float s) {
    return glm::scale(glm::translate(glm::mat4(1.0f), t), glm::vec3(s));
}
// shadow for fireflies and pebbles 
static inline glm::mat4 TRS3(const glm::vec3& t, const glm::vec3& s) {
    return glm::scale(glm::translate(glm::mat4(1.0f), t), s);
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
    glm::vec4 lightColor = glm::vec4(1.00f, 0.86f, 0.72f, 1.0f);   // just to make more romantic btw
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
    Body sun { 0.12f, 0.00f, 0.00f, glm::vec3(1.05f, 0.75f, 0.45f), 0.0f };
    std::vector<Body> planets = {
        { 0.045f, 0.36f, 1.20f, glm::vec3(0.95f, 0.70f, 0.75f), 0.00f }, // blush
        { 0.055f, 0.54f, 0.80f, glm::vec3(0.98f, 0.80f, 0.60f), 0.60f }, // peach
        { 0.065f, 0.78f, 0.55f, glm::vec3(0.80f, 0.65f, 0.95f), 1.10f }, // lavender
    };

    // randomly generate pebbles and butterflies
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_real_distribution<float> uTheta(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> urPeb(0.020f, 0.045f);  // size
    std::uniform_real_distribution<float> redVar(0.0f, 0.15f);    // slight hue shift

    pebbles.clear();
    const float pebbleAreaRadius = 20.0f; // floor coverage
    const int   pebbleCount      = 800;   // more pebbles for the bigger area

    for (int i = 0; i < pebbleCount; ++i) {
        float r  = pebbleAreaRadius * std::sqrt(u01(rng));  // uniform dosl
        float th = uTheta(rng);
        glm::vec3 p(r * std::cos(th), 0.012f, r * std::sin(th)); // increase z, avoid problems
        glm::vec3 c(1.0f, 0.10f + redVar(rng), 0.10f + 0.5f*redVar(rng)); // red
        pebbles.push_back({ p, urPeb(rng), c });
    }

    // a ridiculous number of fireflies lmao, even more ridulous now
    fireflies.clear();

    const int   fireflyCount = 500;         
    const float fireflyMinY  = 0.8f;
    const float fireflyMaxY  = 1.6f;

    std::uniform_real_distribution<float> uHeight(fireflyMinY, fireflyMaxY);
    std::uniform_real_distribution<float> uPhase(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> uRadius(0.012f, 0.022f);
    // to get random color fireflies bias toward warmer colors
    auto randHueRomantic = [&]() {
        float r = u01(rng);
        return (r < 0.6f) ? (0.00f + (r/0.6f)*0.12f)        
                        : (0.83f + ((r-0.6f)/0.4f)*0.17f); 
    };
    std::uniform_real_distribution<float> uSat(0.55f, 0.85f);
    std::uniform_real_distribution<float> uVal(0.92f, 1.00f);


    for (int i = 0; i < fireflyCount; ++i) {
        float rr = pebbleAreaRadius * std::sqrt(u01(rng));
        float th = uTheta(rng);
        glm::vec3 pos(rr * std::cos(th), uHeight(rng), rr * std::sin(th));

        glm::vec3 col = hsv2rgb(randHueRomantic(), uSat(rng), uVal(rng));
        fireflies.push_back({ pos, uRadius(rng), uPhase(rng), col });
    }

    // meteor drag
    double nextMeteorAt = glfwGetTime() + 2.0;



    Container box;
    box.min = glm::vec3(-2.0f, 0.0f, -2.0f);
    box.max = glm::vec3(+2.0f, 2.4f, +2.0f);
    box.restitution = 0.35f;
    box.friction = 0.6f;
        auto spawnMeteor = [&](float yDrop){

    // meteor spawn
    std::uniform_real_distribution<float> uXZ(box.min.x * 0.8f, box.max.x * 0.8f);
    glm::vec3 c(uXZ(rng), yDrop, uXZ(rng));
    float half = 0.16f;                          
    glm::vec3 v0(0.0f, -3.5f, 0.0f);              // falling velocity
    glm::vec3 w0(0.0f);                           // no spin
    float k = 0.10f, damp = 0.020f;               
    int   res = 3;

    float h = randHueRomantic();
    glm::vec3 rgb = hsv2rgb(h, uSat(rng), uVal(rng));
    glm::vec4 tint(rgb, 1.0f);

    Jelly mj(c, half, v0, w0, k, damp, res);
    meteors.emplace_back(mj, tint);
    };

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

     // romantic fog setup (vec3 color)
    glUniform3f(glGetUniformLocation(shader.ID, "fogColor"), 0.12f, 0.05f, 0.10f);
    glUniform1f(glGetUniformLocation(shader.ID, "fogStart"), 8.0f);
    glUniform1f(glGetUniformLocation(shader.ID, "fogEnd"),   40.0f);

    glUniform1f(glGetUniformLocation(shader.ID, "jellyWrap"),      0.4f);
    glUniform1f(glGetUniformLocation(shader.ID, "jellySpecular"),  0.7f);
    glUniform1f(glGetUniformLocation(shader.ID, "jellyShininess"), 64.0f);
    glUniform4f(glGetUniformLocation(shader.ID, "jellyTint"), 1.0f, 1.0f, 1.0f, 1.0f); // match the alpha above

    // Fixed-timestep physics
    double prevTime = glfwGetTime();
    double accumulator = 0.0;
    const double fixedDt = 1.0 / 90.0;

    while (!glfwWindowShouldClose(window)) { // render loo entrance
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glClearColor(0.12f, 0.05f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Toggle stress heatmap with V key
        static bool vWasDown = false;
        int vNow = glfwGetKey(window, GLFW_KEY_V);
        if (vNow == GLFW_PRESS && !vWasDown) {
            j1.showStress = !j1.showStress;
            j2.showStress = !j2.showStress;
            std::cout << "Stress heatmap: " << (j1.showStress ? "ON" : "OFF") << "\n";
        }
        vWasDown = (vNow == GLFW_PRESS);

        if (!dragging) camera.Inputs(window);
        // handles meteor cat
        static bool mWasDown = false;
        int mNow = glfwGetKey(window, GLFW_KEY_M);
        if (mNow == GLFW_PRESS && !mWasDown) { spawnMeteor(box.max.y + 0.8f); }
        mWasDown = (mNow == GLFW_PRESS);
        // camera.updateMatrix(45.0f, 0.1f, 100.0f); //regular camera
        glm::vec3 savedPos = camera.Position;
        if (shakeTimer > 0.0f) {
            float s = 0.04f * (shakeTimer / 0.25f); // camera shakes decay
            auto jitter = [&](float a){ return (u01(rng) - 0.5f) * 2.0f * a; };
            camera.Position += glm::vec3(jitter(s), jitter(s * 0.2f), jitter(s));
        }
        camera.updateMatrix(45.0f, 0.1f, 100.0f);
        camera.Position = savedPos;
        if (glfwGetTime() > nextMeteorAt && meteors.size() < 4) {
            spawnMeteor(box.max.y + 0.8f);
            nextMeteorAt = glfwGetTime() + 3.5; // every 2.25 secnds
        }

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
            float dt = (float)(fixedDt * timeScale); // one line shockwave
            j1.Update(dt, box);
            j2.Update(dt, box);
            j1.CollideWith(j2);
            //meteor (1st for is with jelly, 2nd is inter-meteor)
            for (auto& m : meteors) {
                m.j.Update(dt, box);
                // if it is the first time that the meteor touches the floor
                // we should have the effect
                if (!m.impacted && m.j.getMin().y <= 0.01f) {
                    rings.push_back({ glm::vec3(m.j.center.x, 0.0f, m.j.center.z), 0.0f, u01(rng) });
                    slowmoTimer = 0.40f;   timeScale = 0.35f;
                    shakeTimer  = 0.25f;
                    m.impacted  = true;
                    //sparks
                        int n = 40;
                    for (int i = 0; i < n; ++i) {
                        float th = uTheta(rng);
                        float sp = 1.0f + 0.8f * u01(rng);
                        glm::vec3 dir = glm::vec3(std::cos(th), 0.4f + 0.6f * u01(rng), std::sin(th));
                        Ember e;
                        e.p = glm::vec3(m.j.center.x, 0.03f, m.j.center.z);
                        e.v = dir * sp;
                        e.t = 0.0f;
                        e.life = 0.9f + 0.6f * u01(rng);
                        e.r = 0.02f + 0.02f * u01(rng);
                        embers.push_back(e);
                    }
                   // more emberry effect
                    scorches.push_back({
                        { m.j.center.x, 0.0f, m.j.center.z }, 0.0f, 6.0f, 0.9f + 0.4f * u01(rng), u01(rng) * glm::two_pi<float>()});
                    // smoke plumes around the impact
                    int ns = 12;
                    for (int i = 0; i < ns; ++i) {
                        float th  = uTheta(rng);
                        float rad = 0.25f + 0.25f * u01(rng);
                        smokes.push_back({
                            { m.j.center.x + std::cos(th)*rad, 0.02f, m.j.center.z + std::sin(th)*rad },
                            { 0.15f*std::cos(th), 0.6f + 0.4f*u01(rng), 0.15f*std::sin(th) },
                            0.0f,
                            1.6f + 0.9f*u01(rng),
                            0.12f + 0.08f*u01(rng)
                        });
                    }
                }
                float moved = glm::length(m.j.center - m.lastCenter);
                //optimize the damn lag
                m.lastCenter = m.j.center;
                m.stillTime = (moved < 0.002f) ? (m.stillTime + dt) : 0.0f;
                m.j.CollideWith(j1);
                m.j.CollideWith(j2);
            }
            // meteor end
            if (dragging && draggedJelly) {
                glm::vec3 smoothed = glm::mix(draggedJelly->center, dragTargetCenter, 0.35f);
                draggedJelly->TeleportToCenter(smoothed);
            }            
            // shockwave
            for (auto& r : rings) r.t += dt;
            rings.erase(std::remove_if(rings.begin(), rings.end(),
                        [](const Ring& r){ return r.t > 1.2f; }), rings.end());
            for (auto& e : embers) {
                e.t += dt;
                e.p += e.v * dt;
                e.v *= 0.98f;            
                e.v.y -= 0.8f * dt;      
            }
            embers.erase(std::remove_if(embers.begin(), embers.end(),
                        [](const Ember& e){ return e.t > e.life; }), embers.end());
            // emberry effect
            for (auto& s : scorches) s.t += dt;
            scorches.erase(std::remove_if(scorches.begin(), scorches.end(),[](const Scorch& s){ return s.t > s.life; }), scorches.end());
            for (auto& s : smokes) {
                s.t += dt;
                s.p += s.v * dt;
                s.v *= 0.985f;      
                s.v.y += 0.15f*dt; 
                s.r += 0.12f*dt;    
            }
            smokes.erase(std::remove_if(smokes.begin(), smokes.end(),
                [](const Smoke& s){ return s.t > s.life; }), smokes.end());

            if (slowmoTimer > 0.0f) {
                slowmoTimer -= dt;
                if (slowmoTimer <= 0.0f) timeScale = 1.0f;
            }
            if (shakeTimer > 0.0f) shakeTimer -= dt;
            accumulator -= fixedDt;
            meteors.erase(std::remove_if(meteors.begin(), meteors.end(),
            [](const Meteor& m){
                return m.stillTime > 2.0f && m.j.center.y < 0.38f; // settled on floor
            }), meteors.end());
        }


        // Common per-frame uniforms
        shader.Activate();
        glUniform3f(glGetUniformLocation(shader.ID, "camPos"), camera.Position.x, camera.Position.y, camera.Position.z);
        camera.Matrix(shader, "camMatrix");
        glUniform1f(glGetUniformLocation(shader.ID, "fogAmount"), 0.85f);

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
        //draw the meteor jelly cats
        GLint tintLocJ = glGetUniformLocation(shader.ID, "jellyTint");

        // rendering two-sided and using alphq
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (auto& m : meteors) {
            glUniform4f(tintLocJ, m.tint.r, m.tint.g, m.tint.b, 1.0f);
            catFace.Bind();  m.j.RenderFace(0);  catFace.Unbind();
            catFur.Bind();   for (int f = 1; f < 6; ++f) m.j.RenderFace(f);  catFur.Unbind();
        }
        glUniform4f(tintLocJ, 1,1,1,1);

        // solar
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
        //test shockwave emiisive
        glUniform1f(specLoc, 0.0f);
        glUniform1f(wrapLoc, 0.0f);

        // -shockwave
        glUniform1f(glGetUniformLocation(shader.ID, "fogAmount"), 0.25f);

        glDepthMask(GL_FALSE);                    
        glDisable(GL_CULL_FACE);
        glBlendFunc(GL_ONE, GL_ONE);       

        for (const auto& r : rings) {
            float flick = 0.6f + 0.4f * std::sin(22.0f * r.t + 6.28318f * r.seed);
            float sBase = 0.35f + r.t * 2.7f;

            glm::vec3 deep = glm::vec3(0.70f, 0.05f, 0.02f); // bloody red
            glm::vec3 mid  = glm::vec3(1.00f, 0.35f, 0.05f); // orange
            glm::vec3 hot  = glm::vec3(1.00f, 0.95f, 0.85f); // white
            glm::vec3 glow = glm::vec3(1.00f, 0.80f, 0.20f); // yellow

            // smoky color oooooh
            glm::mat4 Mo = TRS3(glm::vec3(r.p.x, 0.02f, r.p.z), glm::vec3(sBase * 1.35f, 0.01f, sBase * 1.35f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mo));
            glUniform4f(tintLoc, glow.r, glow.g, glow.b, glm::max(0.0f, 0.35f - r.t * 0.25f) * flick);
            sphere.draw();

            // mid ring
            glm::mat4 Mm = TRS3(glm::vec3(r.p.x, 0.02f, r.p.z), glm::vec3(sBase, 0.01f, sBase));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mm));
            glUniform4f(tintLoc, mid.r, mid.g, mid.b, glm::max(0.0f, 0.55f - r.t * 0.45f) * flick);
            sphere.draw();

            // inner core color
            glm::mat4 Mi = TRS3(glm::vec3(r.p.x, 0.02f, r.p.z), glm::vec3(sBase * 0.65f, 0.01f, sBase * 0.65f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mi));
            glUniform4f(tintLoc, hot.r, hot.g, hot.b, glm::max(0.0f, 0.30f - r.t * 0.35f) * (0.7f + 0.3f * flick));
            sphere.draw();

            // little bloody red
            glm::mat4 Md = TRS3(glm::vec3(r.p.x, 0.02f, r.p.z), glm::vec3(sBase * 1.05f, 0.01f, sBase * 1.05f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Md));
            glUniform4f(tintLoc, deep.r, deep.g, deep.b, glm::max(0.0f, 0.25f - r.t * 0.20f) * 0.6f * flick);
            sphere.draw();
        }


        for (const auto& e : embers) {
            float life01 = 1.0f - (e.t / e.life);
            glm::vec3 col = glm::mix(glm::vec3(1.0f, 0.20f, 0.05f),  
                                    glm::vec3(1.0f, 0.80f, 0.20f), 
                                    1.0f - life01);
            //faded Alan walker
            float a = 0.6f * life01;
            glm::mat4 Me = TRS(e.p, e.r * (0.6f + 0.8f * (1.0f - life01)));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Me));
            glUniform4f(tintLoc, col.r, col.g, col.b, a);
            sphere.draw();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform1f(specLoc, 0.0f);
        glUniform1f(wrapLoc, 0.0f);
        //emberry effect
        for (const auto& s : smokes) {
            float life01 = 1.0f - (s.t / s.life);
            float a = 0.35f * life01 * life01;                 
            glm::vec3 col(0.02f, 0.01f, 0.01f);                
            float ySquash = 0.6f + 0.4f * (1.0f - life01);     
            glm::mat4 Ms = glm::scale(glm::translate(glm::mat4(1.0f), s.p),
                                    glm::vec3(s.r, s.r * ySquash, s.r));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Ms));
            glUniform4f(tintLoc, col.r, col.g, col.b, a);
            sphere.draw();
        }
        for (const auto& sc : scorches) {
            float life01 = 1.0f - (sc.t / sc.life);
            float a = 0.42f * life01 * life01;                 
            float r = sc.r * (1.0f + 0.15f * (1.0f - life01)); 
            float jag = 1.0f + 0.06f * std::sin(7.0f * sc.rot + 2.3f * sc.t);
            glm::mat4 Ms = TRS3(glm::vec3(sc.p.x, 0.001f, sc.p.z),
                                glm::vec3(r * jag, 0.01f, r));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Ms));
            glUniform4f(tintLoc, 0.02f, 0.00f, 0.00f, a);      
            sphere.draw();
        }

        glDepthMask(GL_TRUE);  // leave blend mode as-is; your code restores right after


        // restore
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glUniform1f(glGetUniformLocation(shader.ID, "fogAmount"), 0.35f);




    // Sun
    {
        glm::mat4 M = TRS(solarCenter, sun.radius);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(M));
        glUniform4f(tintLoc, sun.color.r, sun.color.g, sun.color.b, 1.0f);
        sphere.draw();

        // Sun halo (additive)
        glBlendFunc(GL_ONE, GL_ONE);
        glm::mat4 Mh = TRS(solarCenter, sun.radius * 1.8f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mh));
        glUniform4f(tintLoc, sun.color.r, sun.color.g, sun.color.b, 0.20f);
        sphere.draw();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        {
            float s = sun.radius * 3.0f;
            glm::vec3 sp(solarCenter.x, 0.001f, solarCenter.z);
            glm::mat4 Ms = TRS3(sp, glm::vec3(s, 0.01f, s));  // squash on Y
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Ms));
            glUniform4f(tintLoc, 0.0f, 0.0f, 0.0f, 0.18f);
            sphere.draw();
        }
    }

    // Planets
    double tsec = glfwGetTime();
    glUniform1f(glGetUniformLocation(shader.ID, "fogAmount"), 0.35f);
    for (const Body& b : planets) {
        float ang = b.phase + (float)tsec * b.speed;
        glm::vec3 p = solarCenter + glm::vec3(std::cos(ang) * b.orbit, 0.0f, std::sin(ang) * b.orbit);

        {
            float s = b.radius * 1.6f;
            glm::vec3 sp(p.x, 0.001f, p.z);
            glm::mat4 Ms = TRS3(sp, glm::vec3(s, 0.01f, s));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Ms));
            glUniform4f(tintLoc, 0.0f, 0.0f, 0.0f, 0.22f);
            sphere.draw();
        }
        // solid pass
        glm::mat4 M = TRS(p, b.radius);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(M));
        glUniform4f(tintLoc, b.color.r, b.color.g, b.color.b, 1.0f);
        sphere.draw();

        // glow pass
        glBlendFunc(GL_ONE, GL_ONE);
        glm::mat4 Mh = TRS(p, b.radius * 1.35f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mh));
        glUniform4f(tintLoc, b.color.r, b.color.g, b.color.b, 0.14f);
        sphere.draw();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glUniform1f(glGetUniformLocation(shader.ID, "fogAmount"), 0.85f);


    //pebbles
    glUniform1f(specLoc, 0.6f);
    glUniform1f(wrapLoc, 0.3f);
    for (const auto& pb : pebbles) {
    glm::mat4 M = TRS(pb.pos, pb.r);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(M));
    glUniform4f(tintLoc, pb.color.r, pb.color.g, pb.color.b, 1.0f);
    sphere.draw();
    }
    //fireflies
    tsec = glfwGetTime();
    for (const auto& f : fireflies) {
        glm::vec3 p = f.pos + glm::vec3(0.0f, 0.07f * std::sin(2.0f * (float)tsec + f.phase), 0.0f);

        {
            float s = f.r * 3.2f;
            glm::vec3 sp(p.x, 0.001f, p.z);
            glUniform1f(specLoc, 0.0f);
            glUniform1f(wrapLoc, 0.0f);
            glm::mat4 Ms = TRS3(sp, glm::vec3(s, 0.01f, s));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Ms));
            glUniform4f(tintLoc, 0.0f, 0.0f, 0.0f, 0.15f);
            sphere.draw();
            glUniform1f(specLoc, 0.6f);
            glUniform1f(wrapLoc, 0.3f);
        }
        // solid core
        glm::mat4 M = TRS(p, f.r);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(M));
        glUniform4f(tintLoc, f.color.r, f.color.g, f.color.b, 1.0f);
        sphere.draw();

        // additive halo
        glBlendFunc(GL_ONE, GL_ONE);
        glm::mat4 Mh = TRS(p, f.r * 2.1f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Mh));
        glUniform4f(tintLoc, f.color.r, f.color.g, f.color.b, 0.50f);
        sphere.draw();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // restore shader params for the rest of the scene
    glUniform1f(specLoc, 0.7f);
    glUniform1f(wrapLoc, 0.4f);
    glUniform4f(tintLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, 0);

        //end solar

    // jelly shadow

    glm::vec4 floorPlane(0, 1, 0, 0);
    glm::vec4 light4(lightPos, 1.0f);
    glm::mat4 S = makePlanarShadow(floorPlane, light4);
    glm::mat4 S_bias = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.002f, 0)) * S;

    shader.Activate();
    camera.Matrix(shader, "camMatrix");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTex);

    const float kShadowAlpha = 0.18f;

    glUniform1f(specLoc, 0.0f);
    glUniform1f(wrapLoc, 0.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(S_bias));
    glUniform4f(tintLoc, 0, 0, 0, kShadowAlpha);
    j1.Render();
    j2.Render();

    glDepthMask(GL_TRUE);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    glUniform4f(tintLoc, 1, 1, 1, 1);
    glUniform1f(specLoc, 0.7f);
    glUniform1f(wrapLoc, 0.4f);


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


