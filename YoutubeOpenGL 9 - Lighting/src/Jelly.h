#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "VAO.h"
#include "VBO.h"
#include "EBO.h"

struct Container {
    glm::vec3 min;   // floor corner
    glm::vec3 max;   // opposite top corner (open top means we only use min.y as floor, max.y for wall height check)
    float restitution = 0.25f;   // bounciness
    float friction = 0.6f;
};

class Jelly {
public:
    Jelly(glm::vec3 center, float radius, glm::vec3 velocity, glm::vec3 acceleration,
        float pointMass, float springStrength, int springsPerEdge);

    void Update(float dt, const Container& box);
    void Render();

    // collisions with another jelly (simple AABB push for starters)
    void CollideWith(Jelly& other);

    // optional fun stuff you already had
    void apply_idle_wobble(float time);
    void apply_punch();
    void resolve_ground_collision(); // kept for compatibility, but container handles it now

    // AABB for broad-phase
    glm::vec3 getMin() const { return aabbMin; }
    glm::vec3 getMax() const { return aabbMax; }

private:
    struct Particle {
        glm::vec3 p;       // current
        glm::vec3 prev;    // previous (for Verlet)
        glm::vec3 a;       // accumulated accel (gravity etc.)
        float invMass;     // 1/mass
    };
    struct Spring {
        int i, j;          // particle indices
        float rest;
        float k;           // stiffness
    };

    void GenerateCubeMesh();             // now builds a grid on each face
    void rebuildIndicesAndAttributes();  // indices/uvs/normals for the current grid layout
    void updateGPU();                    // push vertex positions to VBO

    // physics
    void integrate(float dt);
    void satisfyConstraints(int iterations);
    void applyGravity();
    void collideWithContainer(const Container& box);
    void updateAABB();
    void dampVel(float factor);

public:
    glm::vec3 center;
    float radius;
    glm::vec3 velocity;
    glm::vec3 acceleration;
    float pointMass;            // per particle mass
    float springStrength;       // base k
    int   springsPerEdge;       // divisions along each edge

private:
    // render buffers (interleaved: pos(3), normal(3), uv(2), color(3) = 11 floats)
    std::vector<GLfloat> vertices;
    std::vector<GLuint>  indices;
    std::vector<GLfloat> originalVertices; // for your wobble/punch toys

    // softbody data
    std::vector<Particle> particles;
    std::vector<Spring>   springs;
    int Nx, Ny, Nz;  // grid points per axis for a cube surface lattice (we’ll duplicate for faces)

    int S = 0; // points per edge = springsPerEdge + 1
    std::vector<std::vector<int>> facePointIdx; // 6 faces, each S*S entries

    // AABB
    glm::vec3 aabbMin, aabbMax;

    // GL
    VAO vao;
    VBO* vbo;
    EBO* ebo;
};


