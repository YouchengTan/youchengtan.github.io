#ifndef JELLY_H
#define JELLY_H

#include <glm/glm.hpp>
#include <vector>
#include "VAO.h"
#include "VBO.h"
#include "EBO.h"

class Jelly {
public:
    Jelly(glm::vec3 center, float radius, glm::vec3 velocity, glm::vec3 acceleration, float pointMass, float springStrength, int springsPerEdge);

    void Render();

private:
    glm::vec3 center;
    float radius;
    glm::vec3 velocity;
    glm::vec3 acceleration;
    float pointMass;
    float springStrength;
    int springsPerEdge;

    std::vector<GLfloat> vertices;
    std::vector<GLuint> indices;

    VAO vao;
    VBO* vbo;
    EBO* ebo;

    void GenerateCubeMesh();
};

#endif

