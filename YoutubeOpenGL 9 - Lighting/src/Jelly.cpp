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

    stress.assign(particles.size(), 0.0f);

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

    // Use +Z face (f=0) and -Z face (f=1) to fetch the shared corner particle indices.
    // f=0 (+Z):  (u,v) = (0,0)->(-x,-y,+z), (S-1,0)->(+x,-y,+z), (S-1,S-1)->(+x,+y,+z), (0,S-1)->(-x,+y,+z)
    // f=1 (-Z):  (0,0)->(+x,-y,-z), (S-1,0)->(-x,-y,-z), (S-1,S-1)->(-x,+y,-z), (0,S-1)->(+x,+y,-z)
    auto idx = [&](int face, int u, int v) { return facePointIdx[face][v * S + u]; };

    // +Z face corners
    const int c_xn_yn_zp = idx(0, 0, 0);
    const int c_xp_yn_zp = idx(0, S - 1, 0);
    const int c_xp_yp_zp = idx(0, S - 1, S - 1);
    const int c_xn_yp_zp = idx(0, 0, S - 1);

    // -Z face corners
    const int c_xp_yn_zn = idx(1, 0, 0);
    const int c_xn_yn_zn = idx(1, S - 1, 0);
    const int c_xn_yp_zn = idx(1, S - 1, S - 1);
    const int c_xp_yp_zn = idx(1, 0, S - 1);

    // Softer than surface/body springs
    const float vertexK = springStrength * 0.45f; // try 0.35�0.6

    auto addSoftSpring = [&](int a, int b, float k) {
        if (a == b) return;
        float rest = glm::length(particles[a].p - particles[b].p);
        springs.push_back({ a, b, rest, k });
        };

    // For reference of face indices (from your faces[] setup above):
    // 0:+Z, 1:-Z, 2:+X, 3:-X, 4:+Y, 5:-Y

    // Corner indices on +X (face=2) and -X (face=3)
    // +X (u->+z, v->+y): (0,0)->(+x,-y,-z), (S-1,0)->(+x,-y,+z), (S-1,S-1)->(+x,+y,+z), (0,S-1)->(+x,+y,-z)
    const int xp_yn_zn = idx(2, 0, 0);
    const int xp_yn_zp = idx(2, S - 1, 0);
    const int xp_yp_zp = idx(2, S - 1, S - 1);
    const int xp_yp_zn = idx(2, 0, S - 1);

    // -X (u->-z, v->+y): (0,0)->(-x,-y,+z), (S-1,0)->(-x,-y,-z), (S-1,S-1)->(-x,+y,-z), (0,S-1)->(-x,+y,+z)
    const int xn_yn_zp = idx(3, 0, 0);
    const int xn_yn_zn = idx(3, S - 1, 0);
    const int xn_yp_zn = idx(3, S - 1, S - 1);
    const int xn_yp_zp = idx(3, 0, S - 1);

    // Corner indices on +Y (face=4) and -Y (face=5)
    // +Y (u->+x, v->+z): (0,0)->(-x,+y,-z), (S-1,0)->(+x,+y,-z), (S-1,S-1)->(+x,+y,+z), (0,S-1)->(-x,+y,+z)
    const int xn_yp_zn_y = idx(4, 0, 0);
    const int xp_yp_zn_y = idx(4, S - 1, 0);
    const int xp_yp_zp_y = idx(4, S - 1, S - 1);
    const int xn_yp_zp_y = idx(4, 0, S - 1);

    // -Y (u->+x, v->-z): (0,0)->(-x,-y,+z), (S-1,0)->(+x,-y,+z), (S-1,S-1)->(+x,-y,-z), (0,S-1)->(-x,-y,-z)
    const int xn_yn_zp_y = idx(5, 0, 0);
    const int xp_yn_zp_y = idx(5, S - 1, 0);
    const int xp_yn_zn_y = idx(5, S - 1, S - 1);
    const int xn_yn_zn_y = idx(5, 0, S - 1);

    // Softer than body springs (tune 0.35�0.6)
    const float axisVertexK = springStrength * 0.45f;

    // ===== Across X: (-x,*,*) <-> (+x,*,*) =====
    addSoftSpring(xn_yn_zn, xp_yn_zn, axisVertexK); // (-x,-y,-z) <-> (+x,-y,-z)
    addSoftSpring(xn_yn_zp, xp_yn_zp, axisVertexK); // (-x,-y,+z) <-> (+x,-y,+z)
    addSoftSpring(xn_yp_zn, xp_yp_zn, axisVertexK); // (-x,+y,-z) <-> (+x,+y,-z)
    addSoftSpring(xn_yp_zp, xp_yp_zp, axisVertexK); // (-x,+y,+z) <-> (+x,+y,+z)

    // ===== Across Y: (*,-y,*) <-> (*,+y,*) =====
    addSoftSpring(xn_yn_zn_y, xn_yp_zn_y, axisVertexK); // (-x,-y,-z) <-> (-x,+y,-z)
    addSoftSpring(xp_yn_zn_y, xp_yp_zn_y, axisVertexK); // (+x,-y,-z) <-> (+x,+y,-z)
    addSoftSpring(xn_yn_zp_y, xn_yp_zp_y, axisVertexK); // (-x,-y,+z) <-> (-x,+y,+z)
    addSoftSpring(xp_yn_zp_y, xp_yp_zp_y, axisVertexK); // (+x,-y,+z) <-> (+x,+y,+z)

    // ===== Across Z: (*,-z,*) <-> (*,+z,*) =====
    addSoftSpring(c_xn_yn_zn, c_xp_yp_zp, vertexK); // (-x,-y,-z) <-> (+x,+y,+z)
    addSoftSpring(c_xp_yn_zn, c_xn_yp_zp, vertexK); // (+x,-y,-z) <-> (-x,+y,+z)
    addSoftSpring(c_xn_yp_zn, c_xp_yn_zp, vertexK); // (-x,+y,-z) <-> (+x,-y,+z)
    addSoftSpring(c_xp_yp_zn, c_xn_yn_zp, vertexK); // (+x,+y,-z) <-> (-x,-y,+z)

    rebuildIndicesAndAttributes();
    updateAABB();
}

void Jelly::rebuildIndicesAndAttributes()
{
    faceIndexRanges.clear();
    faceIndexRanges.reserve(6);
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

    //const glm::vec3 color(1.0f, 0.2f, 0.6f);
    auto heatColor = [](float t)->glm::vec3 {
        t = std::pow(std::clamp(t, 0.0f, 1.0f), 0.5f);

        if (t < 0.25f) { // blue to cyan
            float k = t / 0.25f;
            return glm::vec3(0.0f, k, 1.0f);
        }
        else if (t < 0.5f) { // cyan to green
            float k = (t - 0.25f) / 0.25f;
            return glm::vec3(0.0f, 1.0f, 1.0f - k);
        }
        else if (t < 0.75f) { // green to yellow
            float k = (t - 0.5f) / 0.25f;
            return glm::vec3(k, 1.0f, 0.0f);
        }
        else { // yellow to red
            float k = (t - 0.75f) / 0.25f;
            return glm::vec3(1.0f, 1.0f - k, 0.0f);
        }
        };
   
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

                float st = (pi >= 0 && pi < (int)stress.size()) ? stress[pi] : 0.0f;
                
                if (!showStress) {
                    st = 0.0f;
                }
                else {
                    st *= 5.0f;
                    if (st > 1.0f) st = 1.0f;
                }
                glm::vec3 color = heatColor(st);

                // pos, normal (flat), uv, color
                vertices.insert(vertices.end(), {
                    p.x,p.y,p.z,
                    fd.normal.x,fd.normal.y,fd.normal.z,
                    uu,vv,
                    color.r,color.g,color.b
                    });
            }
        }
        GLuint idxStart = (GLuint)indices.size();

        for (int v = 0; v < S - 1; ++v) {
            for (int u = 0; u < S - 1; ++u) {
                GLuint i0 = base + v * S + u;
                GLuint i1 = base + v * S + (u + 1);
                GLuint i2 = base + (v + 1) * S + (u + 1);
                GLuint i3 = base + (v + 1) * S + u;
                indices.insert(indices.end(), { i0,i1,i2,  i0,i2,i3 });
            }
        }

        GLuint count = (GLuint)indices.size() - idxStart;
        faceIndexRanges.push_back({ idxStart, (GLsizei)count });
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

            float strain = std::abs(c) / (s.rest + 1e-6f);
            float s01 = std::min(1.0f, strain * 0.8f);
            stress[s.i] = std::max(stress[s.i], s01);
            stress[s.j] = std::max(stress[s.j], s01);

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

int Jelly::ClosestParticleTo(const glm::vec3& p) const
{
    int best = -1;
    float bestD2 = 1e30f;
    for (int i = 0; i < (int)particles.size(); ++i)
    {
        float d2 = glm::length2(particles[i].p - p);
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

void Jelly::SetPin(int particleIndex, const glm::vec3& targetWorld, float stiffness, bool zeroVelocity)
{
    hasPin = (particleIndex >= 0 && particleIndex < (int)particles.size());
    pinIdx = hasPin ? particleIndex : -1;
    pinTarget = targetWorld;
    pinAlpha = glm::clamp(stiffness, 0.0f, 1.0f);
    pinZeroVel = zeroVelocity;
}

void Jelly::UpdatePinTarget(const glm::vec3& targetWorld)
{
    if (!hasPin) return;
    pinTarget = targetWorld;
}

void Jelly::ClearPin()
{
    hasPin = false;
    pinIdx = -1;
}

void Jelly::Update(float dt, const Container& box)
{
    for (auto& p : particles) p.a += acceleration;
    applyGravity();
    integrate(dt);

    // --- APPLY PIN CONSTRAINT (mouse drag) ---
    if (hasPin && pinIdx >= 0 && pinIdx < (int)particles.size())
    {
        auto& pt = particles[pinIdx];

        // Move toward target with strength pinAlpha
        glm::vec3 newP = glm::mix(pt.p, pinTarget, pinAlpha);

        if (pinZeroVel) {
            pt.prev = newP; // zero velocity
        }
        else {
            // keep some velocity continuity but steer toward target
            glm::vec3 v = (newP - pt.prev); // implicit velocity
            pt.prev = newP - v * 0.2f;      // damp a bit to keep stable
        }
        pt.p = newP;
    }

    for (float& st : stress) st *= 0.92f;

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

void Jelly::RenderFace(int f)
{
    if (f < 0 || f >= (int)faceIndexRanges.size()) return;
    const auto& range = faceIndexRanges[f];

    vao.Bind();
    glDrawElements(GL_TRIANGLES, range.second, GL_UNSIGNED_INT,
                   (void*)(size_t)(range.first * sizeof(GLuint)));
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

    glm::vec3 pen(
        std::min(amax.x - bmin.x, bmax.x - amin.x),
        std::min(amax.y - bmin.y, bmax.y - amin.y),
        std::min(amax.z - bmin.z, bmax.z - amin.z)
    );
    float mpen = std::min(std::min(pen.x, pen.y), pen.z);

    glm::vec3 aC = 0.5f * (amin + amax);
    glm::vec3 bC = 0.5f * (bmin + bmax);
    Jelly* top    = (aC.y >= bC.y) ? this : &other;
    Jelly* bottom = (aC.y >= bC.y) ? &other : this;

    // center of mass
    auto comOf = [](const std::vector<Particle>& pts)->glm::vec3 {
        double sumw = 0.0;
        glm::dvec3 c(0.0);
        for (const auto& p : pts) {
            double w = (p.invMass > 0.0f) ? 1.0 / double(p.invMass) : 0.0;
            c += glm::dvec3(p.p) * w; sumw += w;
        }
        if (sumw > 0.0) c /= sumw;
        return glm::vec3(c);
    };
    glm::vec3 comTop = comOf(top->particles);

    // AABB to XZ coord
    float ox0 = std::max(amin.x, bmin.x);
    float ox1 = std::min(amax.x, bmax.x);
    float oz0 = std::max(amin.z, bmin.z);
    float oz1 = std::min(amax.z, bmax.z);
    float ow  = std::max(0.0f, ox1 - ox0);
    float od  = std::max(0.0f, oz1 - oz0);

    // Distance of top 
    auto clampf32 = [](float x, float a, float b){ return std::max(a, std::min(b, x)); };
    glm::vec2 proj(comTop.x, comTop.z);
    glm::vec2 clamped(clampf32(proj.x, ox0, ox1), clampf32(proj.y, oz0, oz1));
    glm::vec2 outside = proj - clamped;
    float distOutside = glm::length(outside);

    // tipping
    float halfMin = 0.5f * std::min(ow, od);
    bool tipping = (halfMin > 0.0f) && (distOutside > 0.15f * halfMin);

    if (tipping) {

        glm::vec2 dir2 = (distOutside > 1e-6f) ? (outside / distOutside)
                                               : glm::normalize(glm::vec2(comTop.x - bC.x, comTop.z - bC.z));
        if (!std::isfinite(dir2.x)) dir2 = glm::vec2(1, 0);

        glm::vec3 tipDir(dir2.x, -0.05f, dir2.y);

        glm::vec3 dTop = tipDir * (0.65f * mpen);
        glm::vec3 dBot = -tipDir * (0.15f * mpen);

        for (auto& p : top->particles)    { p.p += dTop; p.prev += dTop; }
        for (auto& p : bottom->particles) { p.p += dBot; p.prev += dBot; }
    } else {
        glm::vec3 axis(0.0f);
        glm::vec3 delta = aC - bC;
        if (glm::length2(delta) < 1e-12f) delta = glm::vec3(0,1,0);

        if (pen.x <= pen.y && pen.x <= pen.z) axis = glm::vec3(delta.x > 0 ? 1.f : -1.f, 0, 0);
        else if (pen.y <= pen.x && pen.y <= pen.z) axis = glm::vec3(0, delta.y > 0 ? 1.f : -1.f, 0);
        else axis = glm::vec3(0, 0, delta.z > 0 ? 1.f : -1.f);

        glm::vec3 d = axis * (0.5f * mpen);
        for (auto& p : particles)       p.p += 0.5f * d;
        for (auto& p : other.particles) p.p -= 0.5f * d;
    }

    updateAABB();
    other.updateAABB();
}
