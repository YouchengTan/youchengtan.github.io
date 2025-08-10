#include "Jelly.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>

static inline float clampf(float x, float a, float b) { return std::max(a, std::min(b, x)); }

Jelly::Jelly(glm::vec3 center_, float radius_, glm::vec3 velocity_, glm::vec3 acceleration_,
    float pointMass_, float springStrength_, int springsPerEdge_)
    : center(center_), radius(radius_), velocity(velocity_), acceleration(acceleration_),
    pointMass(pointMass_), springStrength(springStrength_), springsPerEdge(springsPerEdge_),
    vbo(nullptr), ebo(nullptr)
{
    GenerateCubeMesh(); // builds particles, springs, render vertices/indices

    originalVertices = vertices;

    vao.Bind();
    vbo = new VBO(vertices.data(), (GLsizeiptr)(vertices.size() * sizeof(GLfloat)));
    ebo = new EBO(indices.data(), (GLsizeiptr)(indices.size() * sizeof(GLuint)));
    vao.LinkAttrib(*vbo, 0, 3, GL_FLOAT, 11 * sizeof(float), (void*)0);                   // pos
    vao.LinkAttrib(*vbo, 1, 3, GL_FLOAT, 11 * sizeof(float), (void*)(3 * sizeof(float))); // normal
    vao.LinkAttrib(*vbo, 2, 2, GL_FLOAT, 11 * sizeof(float), (void*)(6 * sizeof(float))); // uv
    vao.LinkAttrib(*vbo, 3, 3, GL_FLOAT, 11 * sizeof(float), (void*)(8 * sizeof(float))); // color
    vao.Unbind(); vbo->Unbind(); ebo->Unbind();

    updateAABB();
}

// Build particles as a per-face grid on a cube and create springs along the grids.
// Surface lattice (not volumetric) for speed.
void Jelly::GenerateCubeMesh()
{
    // Use MEMBER S and MEMBER facePointIdx
    S = std::max(2, springsPerEdge + 1);
    facePointIdx.assign(6, std::vector<int>(S * S, -1));

    particles.clear(); springs.clear(); vertices.clear(); indices.clear();

    const float half = radius * 0.5f;

    auto addParticle = [&](const glm::vec3& pos) {
        Particle p{};
        p.p = pos; p.prev = pos; p.a = glm::vec3(0.0f);
        p.invMass = (pointMass > 0.0f) ? (1.0f / pointMass) : 0.0f;
        particles.push_back(p);
        return (int)particles.size() - 1;
        };

    struct FaceDef { glm::vec3 origin, ex, ey, normal; };
    std::vector<FaceDef> faces = {
        { center + glm::vec3(-half, -half, +half), glm::vec3(radius / (S - 1),0,0), glm::vec3(0, radius / (S - 1),0), glm::vec3(0,0, 1) },
        { center + glm::vec3(half, -half, -half), glm::vec3(-radius / (S - 1),0,0), glm::vec3(0, radius / (S - 1),0), glm::vec3(0,0,-1) },
        { center + glm::vec3(+half, -half, -half), glm::vec3(0,0, radius / (S - 1)), glm::vec3(0, radius / (S - 1),0), glm::vec3(1,0, 0) },
        { center + glm::vec3(-half, -half, +half), glm::vec3(0,0,-radius / (S - 1)), glm::vec3(0, radius / (S - 1),0), glm::vec3(-1,0,0) },
        { center + glm::vec3(-half, +half, -half), glm::vec3(radius / (S - 1),0,0), glm::vec3(0,0, radius / (S - 1)), glm::vec3(0,1, 0) },
        { center + glm::vec3(-half, -half, +half), glm::vec3(radius / (S - 1),0,0), glm::vec3(0,0,-radius / (S - 1)), glm::vec3(0,-1,0) },
    };

    auto keyOf = [&](const glm::vec3& p)->glm::ivec3 {
        const float q = 1e-4f;
        return glm::ivec3((int)std::round(p.x / q), (int)std::round(p.y / q), (int)std::round(p.z / q));
        };
    struct KeyHash {
        size_t operator()(const glm::ivec3& k) const noexcept {
            return ((size_t)k.x * 73856093) ^ ((size_t)k.y * 19349663) ^ ((size_t)k.z * 83492791);
        }
    };
    std::unordered_map<glm::ivec3, int, KeyHash> lut;

    // Fill the MEMBER facePointIdx
    for (int f = 0; f < 6; ++f) {
        const auto& fd = faces[f];
        for (int v = 0; v < S; ++v) {
            for (int u = 0; u < S; ++u) {
                glm::vec3 pos = fd.origin + fd.ex * (float)u + fd.ey * (float)v;
                auto k = keyOf(pos);
                auto it = lut.find(k);
                int idx;
                if (it == lut.end()) { idx = addParticle(pos); lut.emplace(k, idx); }
                else { idx = it->second; }
                facePointIdx[f][v * S + u] = idx;
            }
        }
    }

    auto addSpring = [&](int a, int b, float k) {
        if (a == b) return;
        float rest = glm::length(particles[a].p - particles[b].p);
        springs.push_back({ a,b,rest,k });
        };
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < S; ++v) {
            for (int u = 0; u < S; ++u) {
                int i = facePointIdx[f][v * S + u];
                if (u + 1 < S) addSpring(i, facePointIdx[f][v * S + (u + 1)], springStrength);
                if (v + 1 < S) addSpring(i, facePointIdx[f][(v + 1) * S + u], springStrength);
                if (u + 1 < S && v + 1 < S) addSpring(i, facePointIdx[f][(v + 1) * S + (u + 1)], springStrength * 0.7f);
                if (u > 0 && v + 1 < S) addSpring(i, facePointIdx[f][(v + 1) * S + (u - 1)], springStrength * 0.7f);
            }
        }
    }

    // Add body springs between opposite faces to preserve thickness.
    // Face pairs: 0 <-> 1 (+Z <-> -Z), 2 <-> 3 (+X <-> -X), 4 <-> 5 (+Y <-> -Y)
    // Because some faces use reversed axes, we mirror (u or v) to match positions.
    auto addPairSprings = [&](int fA, int fB, bool mirrorU, bool mirrorV, float k) {
        for (int v = 0; v < S; ++v) {
            for (int u = 0; u < S; ++u) {
                int ua = u, va = v;
                int ub = mirrorU ? (S - 1 - u) : u;
                int vb = mirrorV ? (S - 1 - v) : v;
                int ia = facePointIdx[fA][va * S + ua];
                int ib = facePointIdx[fB][vb * S + ub];
                if (ia == ib) continue;           // edges/corners may coincide via LUT
                float rest = glm::length(particles[ia].p - particles[ib].p);
                springs.push_back({ ia, ib, rest, k });
            }
        }
        };

    // Slightly softer than surface springs so they stabilize without getting too stiff
    const float bodyK = springStrength * 0.6f;
    addPairSprings(0, 1, /*mirrorU=*/true,  /*mirrorV=*/false, bodyK); // +Z <-> -Z
    addPairSprings(2, 3, /*mirrorU=*/true,  /*mirrorV=*/false, bodyK); // +X <-> -X
    addPairSprings(4, 5, /*mirrorU=*/false, /*mirrorV=*/true, bodyK); // +Y <-> -Y

    rebuildIndicesAndAttributes();
    updateAABB();
}

void Jelly::rebuildIndicesAndAttributes()
{
    vertices.clear();
    indices.clear();

    const float half = radius * 0.5f;

    struct FaceDef { glm::vec3 normal, axisU, axisV, origin; };
    std::vector<FaceDef> fdef = {
        {{0,0,1},{1,0,0},{0,1,0}, center + glm::vec3(-half,-half,+half)}, // +Z
        {{0,0,-1},{-1,0,0},{0,1,0}, center + glm::vec3(+half,-half,-half)},// -Z
        {{1,0,0},{0,0,1},{0,1,0}, center + glm::vec3(+half,-half,-half)}, // +X
        {{-1,0,0},{0,0,-1},{0,1,0}, center + glm::vec3(-half,-half,+half)},// -X
        {{0,1,0},{1,0,0},{0,0,1}, center + glm::vec3(-half,+half,-half)}, // +Y
        {{0,-1,0},{1,0,0},{0,0,-1},center + glm::vec3(-half,-half,+half)} // -Y
    };

    const glm::vec3 color(1.0f, 0.2f, 0.6f);

    for (int f = 0; f < 6; ++f) {
        const auto& fd = fdef[f];
        const GLuint base = (GLuint)(vertices.size() / 11);

        for (int v = 0; v < S; ++v) {
            for (int u = 0; u < S; ++u) {
                int pi = facePointIdx[f][v * S + u];
                if (pi < 0 || pi >= (int)particles.size()) pi = 0; // fallback to a valid index
                const glm::vec3 p = particles[pi].p; // LIVE particle position

                float uu = (float)u / (float)(S - 1);
                float vv = (float)v / (float)(S - 1);

                // pos, normal (flat), uv, color
                vertices.insert(vertices.end(), {
                    p.x,p.y,p.z,
                    fd.normal.x,fd.normal.y,fd.normal.z,
                    uu,vv,
                    color.r,color.g,color.b
                    });
            }
        }
        for (int v = 0; v < S - 1; ++v) {
            for (int u = 0; u < S - 1; ++u) {
                GLuint i0 = base + v * S + u;
                GLuint i1 = base + v * S + (u + 1);
                GLuint i2 = base + (v + 1) * S + (u + 1);
                GLuint i3 = base + (v + 1) * S + u;
                indices.insert(indices.end(), { i0,i1,i2,  i0,i2,i3 });
            }
        }
    }
}

void Jelly::updateGPU()
{
    vbo->Bind();
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(vertices.size() * sizeof(GLfloat)), vertices.data());
}

void Jelly::applyGravity() { for (auto& p : particles) p.a += glm::vec3(0, -9.81f, 0); }

void Jelly::integrate(float dt)
{
    const float damping = 0.01f; // mild global damping
    for (auto& p : particles) {
        glm::vec3 temp = p.p;
        glm::vec3 vel = (p.p - p.prev) * (1.0f - damping);
        p.p = p.p + vel + p.a * (dt * dt);
        p.prev = temp;
        p.a = glm::vec3(0.0f);
    }
}
void Jelly::satisfyConstraints(int iterations)
{
    const float SOFTNESS = 0.35f;
    const float MAX_CORR = 0.010f * radius;
    const float k_total  = 0.45f;
    const float k_iter   = 1.0f - std::pow(1.0f - k_total, 1.0f / iterations);

    for (int it = 0; it < iterations; ++it) {
        for (const auto& s : springs) {
            auto& a = particles[s.i];
            auto& b = particles[s.j];

            glm::vec3 d = b.p - a.p;
            float l2 = glm::length2(d);
            if (l2 < 1e-12f) continue;

            float len = std::sqrt(l2);
            float c   = (len - s.rest);
            glm::vec3 n = d / len;

            float w1 = a.invMass, w2 = b.invMass, wsum = w1 + w2;
            if (wsum <= 0.0f) continue;

            glm::vec3 corr = SOFTNESS * k_iter * c * n;

            float L = glm::length(corr);
            if (L > MAX_CORR && L > 0.0f) corr *= (MAX_CORR / L);

            a.p += (w1 / wsum) * corr;
            b.p -= (w2 / wsum) * corr;
        }
    }
}

void Jelly::collideWithContainer(const Container& box)
{
    const float EPS = 1e-4f;
    // walls: x in [min.x, max.x], z in [min.z, max.z], y >= min.y (floor), open top
    for (auto& p : particles) {
        glm::vec3 cur = p.p;
        glm::vec3 prev = p.prev;

        // floor
        if (cur.y < box.min.y) {
            cur.y = box.min.y + EPS;;
            glm::vec3 v = cur - prev;
            v.y = -v.y * (1.0f - box.restitution);
            v.x *= (1.0f - box.friction);
            v.z *= (1.0f - box.friction);
            prev = cur - v;
        }
        // x walls
        if (cur.x < box.min.x) {
            cur.x = box.min.x + EPS;
            glm::vec3 v = cur - prev; v.x = -v.x * (1.0f - box.restitution);
            v.y *= (1.0f - box.friction); v.z *= (1.0f - box.friction);
            prev = cur - v;
        }
        else if (cur.x > box.max.x) {
            cur.x = box.max.x - EPS;
            glm::vec3 v = cur - prev; v.x = -v.x * (1.0f - box.restitution);
            v.y *= (1.0f - box.friction); v.z *= (1.0f - box.friction);
            prev = cur - v;
        }
        // z walls
        if (cur.z < box.min.z) {
            cur.z = box.min.z + EPS;
            glm::vec3 v = cur - prev; v.z = -v.z * (1.0f - box.restitution);
            v.y *= (1.0f - box.friction); v.x *= (1.0f - box.friction);
            prev = cur - v;
        }
        else if (cur.z > box.max.z) {
            cur.z = box.max.z - EPS;
            glm::vec3 v = cur - prev; v.z = -v.z * (1.0f - box.restitution);
            v.y *= (1.0f - box.friction); v.x *= (1.0f - box.friction);
            prev = cur - v;
        }

        p.p = cur; p.prev = prev;
    }
}

void Jelly::updateAABB()
{
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (auto& p : particles) { mn = glm::min(mn, p.p); mx = glm::max(mx, p.p); }
    aabbMin = mn; aabbMax = mx;
}

void Jelly::Update(float dt, const Container& box)
{
    for (auto& p : particles) p.a += acceleration;
    applyGravity();
    integrate(dt);

    const int iters = 4;
    for (int i = 0; i < iters; ++i) {
        collideWithContainer(box);   // project onto container planes
        satisfyConstraints(2);       // then spring projection
    }
    dampVel(0.996f); // softer

    updateAABB();
    rebuildIndicesAndAttributes();
    updateGPU();
}

void Jelly::dampVel(float factor)
{
    for (auto& p : particles)
    {
        glm::vec3 v = p.p - p.prev;
        v *= factor;
        p.prev = p.p - v;   
    }
}


void Jelly::Render()
{
    vao.Bind();
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
    vao.Unbind();
}

void Jelly::TeleportToCenter(const glm::vec3& newCenter)
{
    glm::vec3 d = newCenter - center;
    center = newCenter;
    for (auto& pt : particles) {
        pt.p   += d;
        pt.prev = pt.p;           // zero velocity
        pt.a    = glm::vec3(0.0f);// clear any accel
    }
    updateAABB();
    rebuildIndicesAndAttributes();
    updateGPU();
}



void Jelly::TranslateAll(const glm::vec3& d)
{
    center += d;
    for (auto& pt : particles) { pt.p += d; pt.prev += d; }
    rebuildIndicesAndAttributes();
    updateAABB();
    updateGPU();
}




void Jelly::apply_idle_wobble(float t)
{
    float amp = 0.01f, freq = 4.0f;
    for (auto& p : particles) {
        glm::vec3 dir = glm::normalize(p.p - center);
        if (!std::isfinite(dir.x)) dir = glm::vec3(0, 1, 0);
        p.p += dir * (amp * std::sin(freq * t));
    }
}

void Jelly::apply_punch()
{
    for (auto& p : particles) {
        if (p.p.z > center.z) p.p.z += 0.05f;
    }
}

void Jelly::resolve_ground_collision()
{
    for (auto& p : particles) if (p.p.y < 0.0f) p.p.y = 0.0f;
}

void Jelly::CollideWith(Jelly& other)
{
    glm::vec3 amin = getMin(), amax = getMax();
    glm::vec3 bmin = other.getMin(), bmax = other.getMax();
    bool overlap = (amin.x <= bmax.x && amax.x >= bmin.x) &&
        (amin.y <= bmax.y && amax.y >= bmin.y) &&
        (amin.z <= bmax.z && amax.z >= bmin.z);
    if (!overlap) return;

    glm::vec3 aCenter = 0.5f * (amin + amax);
    glm::vec3 bCenter = 0.5f * (bmin + bmax);
    glm::vec3 delta = aCenter - bCenter;
    if (glm::length2(delta) < 1e-12f) delta = glm::vec3(0, 1, 0);
    glm::vec3 pen(
        std::min(amax.x - bmin.x, bmax.x - amin.x),
        std::min(amax.y - bmin.y, bmax.y - amin.y),
        std::min(amax.z - bmin.z, bmax.z - amin.z)
    );
    if (pen.x <= pen.y && pen.x <= pen.z) delta = glm::vec3(delta.x > 0 ? 1 : -1, 0, 0);
    else if (pen.y <= pen.x && pen.y <= pen.z) delta = glm::vec3(0, delta.y > 0 ? 1 : -1, 0);
    else delta = glm::vec3(0, 0, delta.z > 0 ? 1 : -1);

    const float push = 0.5f * std::min(std::min(pen.x, pen.y), pen.z);
    for (auto& p : particles)        p.p += delta * push * 0.5f;
    for (auto& p : other.particles)  p.p -= delta * push * 0.5f;
    updateAABB(); other.updateAABB();
}


