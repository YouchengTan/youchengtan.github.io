#include "Jelly.h"
#include <glm/gtc/type_ptr.hpp>

Jelly::Jelly(glm::vec3 center, float radius, glm::vec3 velocity, glm::vec3 acceleration, float pointMass, float springStrength, int springsPerEdge)
    : center(center),
    radius(radius),
    velocity(velocity),
    acceleration(acceleration),
    pointMass(pointMass),
    springStrength(springStrength),
    springsPerEdge(springsPerEdge),
    vbo(nullptr),
    ebo(nullptr)
{
    GenerateCubeMesh();

    // Save the initial vertex positions
    originalVertices = vertices;

    vao.Bind();
    vbo = new VBO(vertices.data(), vertices.size() * sizeof(GLfloat));
    ebo = new EBO(indices.data(), indices.size() * sizeof(GLuint));

    vao.LinkAttrib(*vbo, 0, 3, GL_FLOAT, 11 * sizeof(float), (void*)0);                // Position
    vao.LinkAttrib(*vbo, 1, 3, GL_FLOAT, 11 * sizeof(float), (void*)(3 * sizeof(float))); // Normal
    vao.LinkAttrib(*vbo, 2, 2, GL_FLOAT, 11 * sizeof(float), (void*)(6 * sizeof(float))); // TexCoords
    vao.LinkAttrib(*vbo, 3, 3, GL_FLOAT, 11 * sizeof(float), (void*)(8 * sizeof(float))); // Color

    vao.Unbind();
    vbo->Unbind();
    ebo->Unbind();
}

void Jelly::GenerateCubeMesh() {
    glm::vec3 half = glm::vec3(radius / 2.0f);

    struct Face {
        glm::vec3 normal;
        glm::vec3 v0, v1, v2, v3;
    };

    std::vector<Face> faces = {
        // Back face (-Z)
        {{ 0,  0, -1},
         center + glm::vec3(-half.x, -half.y, -half.z),
         center + glm::vec3(half.x, -half.y, -half.z),
         center + glm::vec3(half.x,  half.y, -half.z),
         center + glm::vec3(-half.x,  half.y, -half.z)},

         // Front face (+Z)
         {{ 0,  0, 1},
          center + glm::vec3(-half.x, -half.y,  half.z),
          center + glm::vec3(half.x, -half.y,  half.z),
          center + glm::vec3(half.x,  half.y,  half.z),
          center + glm::vec3(-half.x,  half.y,  half.z)},

          // Left face (-X)
          {{ -1, 0, 0},
           center + glm::vec3(-half.x, -half.y,  half.z),
           center + glm::vec3(-half.x, -half.y, -half.z),
           center + glm::vec3(-half.x,  half.y, -half.z),
           center + glm::vec3(-half.x,  half.y,  half.z)},

           // Right face (+X)
           {{ 1, 0, 0},
            center + glm::vec3(half.x, -half.y, -half.z),
            center + glm::vec3(half.x, -half.y,  half.z),
            center + glm::vec3(half.x,  half.y,  half.z),
            center + glm::vec3(half.x,  half.y, -half.z)},

            // Bottom face (-Y)
            {{ 0, -1, 0},
             center + glm::vec3(-half.x, -half.y,  half.z),
             center + glm::vec3(half.x, -half.y,  half.z),
             center + glm::vec3(half.x, -half.y, -half.z),
             center + glm::vec3(-half.x, -half.y, -half.z)},

             // Top face (+Y)
             {{ 0, 1, 0},
              center + glm::vec3(-half.x, half.y, -half.z),
              center + glm::vec3(half.x, half.y, -half.z),
              center + glm::vec3(half.x, half.y,  half.z),
              center + glm::vec3(-half.x, half.y,  half.z)}
    };

    for (const auto& face : faces) {
        glm::vec3 color(1.0f, 0.2f, 0.6f); // default jelly pink color

        // Use full texture per face: (0,0), (1,0), (1,1), (0,1)
        glm::vec2 texCoords[4] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        };

        glm::vec3 positions[4] = { face.v0, face.v1, face.v2, face.v3 };

        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos = positions[i];
            glm::vec2 tex = texCoords[i];
            vertices.insert(vertices.end(), {
                pos.x, pos.y, pos.z,
                face.normal.x, face.normal.y, face.normal.z,
                tex.x, tex.y,
                color.r, color.g, color.b
                });
        }
    }

    // 6 faces * 2 triangles * 3 indices = 36
    for (int i = 0; i < 6; ++i) {
        GLuint base = i * 4;
        indices.insert(indices.end(), {
            base, base + 1, base + 2,
            base, base + 2, base + 3
            });
    }
}


void Jelly::Render() {
    vao.Bind();
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
    vao.Unbind();
}

void Jelly::apply_idle_wobble(float time) {
    // Reset to original vertices before applying punch
    vertices = originalVertices;
    float freq = 4.0f;   // breathing frequency
    float amp = 0.01f;   // amplitude of the wobble

    // Update positions in the vertex buffer
    for (size_t i = 0; i < vertices.size(); i += 11) {
        glm::vec3 pos(vertices[i], vertices[i + 1], vertices[i + 2]);
        glm::vec3 dir = glm::normalize(pos - center);
        pos += dir * (amp * sin(freq * time));
        vertices[i] = pos.x;
        vertices[i + 1] = pos.y;
        vertices[i + 2] = pos.z;
    }

    // Upload updated vertex data to the GPU
    vbo->Bind();
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(GLfloat), vertices.data());
}

void Jelly::apply_punch() {
    vertices = originalVertices;
    glm::vec3 punchDir(0, 0, 1); // forward direction for the punch
    float punchForce = 0.05f;    // displacement amount

    for (size_t i = 0; i < vertices.size(); i += 11) {
        if (vertices[i + 2] > center.z) { // only push vertices in front
            vertices[i] += punchDir.x * punchForce;
            vertices[i + 1] += punchDir.y * punchForce;
            vertices[i + 2] += punchDir.z * punchForce;
        }
    }

    vbo->Bind();
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(GLfloat), vertices.data());
}

void Jelly::resolve_ground_collision() {
    float groundY = 0.0f;

    for (size_t i = 0; i < vertices.size(); i += 11) {
        if (vertices[i + 1] < groundY) {
            vertices[i + 1] = groundY; // clamp vertex to ground level
        }
    }

    vbo->Bind();
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(GLfloat), vertices.data());
}

