#include "Physics3D.h"
#include "Components/Transform.h"
#include "Components/RigidBody.h"   // Include your new component
#include "Components/BoxCollider.h" // Include your new component
#include "Components/HeightfieldCollider.h"
#include "Collision/Octree.h"       //
#include <algorithm>

#include "Components/StaticMesh.h"
#include "Api/AssetManagerApi.h"

// Include Colliders
#include "BoxCollider.h"
#include "CapsuleCollider.h"
#include "MeshCollider.h"

// --- Helper Math: Point vs Triangle ---
glm::vec3 ClosestPointTriangle(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;
    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

void Physics3D::Update(float dt) {
    // 1. Auto-fit logic for simple props
    SyncStaticColliders();

    activeColliders.clear();
    aabbToEntity.clear();

    // 2. Integration & AABB Update
    for (Entity e : mEntities) {
        // Read from Current State
        auto* tRead = static_cast<Transform*>(engine_->GetComponent(e, "Transform"));
        auto* rb = static_cast<RigidBody*>(engine_->GetComponent(e, "RigidBody"));

        if (!tRead || !rb) continue;

        // Write to Next State (Double Buffering)
        auto* tWrite = static_cast<Transform*>(engine_->GetComponentForWrite(e, "Transform"));

        // Ensure tWrite starts with valid data (in case no movement happens)
        // Note: If your SwapBuffers just swaps pointers, tWrite actually has OLD data. 
        // We usually base calculations on tRead and overwrite tWrite completely.
        tWrite->scale = tRead->scale;

        if (!rb->isStatic) {
            // Update Physical Constants (Just in case they changed)
            if (rb->mass > 0.0f) rb->inverseMass = 1.0f / rb->mass;
            else rb->inverseMass = 0.0f;

            // Apply Gravity
            rb->velocity += (gravity + rb->acceleration) * dt;

            // Linear Integration
            tWrite->position = tRead->position + (rb->velocity * dt);

            // Angular Integration (Quaternion)
            // Spin = 0.5 * W * Q
            glm::quat spin = glm::quat(0, rb->angularVelocity.x, rb->angularVelocity.y, rb->angularVelocity.z);
            tWrite->rotation = tRead->rotation + (0.5f * spin * tRead->rotation) * dt;
            tWrite->rotation = glm::normalize(tWrite->rotation);

            // Damping
            rb->velocity *= 0.995f;
            rb->angularVelocity *= 0.98f;

            engine_->MarkEntityDirty(e, "Transform");
        }
        else {
            // Static objects just copy position
            tWrite->position = tRead->position;
            tWrite->rotation = tRead->rotation;
        }

        // Update Component AABBs
        if (engine_->HasComponent(e, "BoxCollider")) {
            auto* box = static_cast<BoxCollider*>(engine_->GetComponent(e, "BoxCollider"));
            box->Update(tWrite->position, tWrite->rotation, tWrite->scale);
            activeColliders.push_back(box->aabb.get());
            aabbToEntity[box->aabb.get()] = e;
        }
        if (engine_->HasComponent(e, "CapsuleCollider")) {
            auto* cap = static_cast<CapsuleCollider*>(engine_->GetComponent(e, "CapsuleCollider"));
            cap->Update(tWrite->position, tWrite->rotation, tWrite->scale);
            activeColliders.push_back(cap->aabb.get());
            aabbToEntity[cap->aabb.get()] = e;
        }
        if (engine_->HasComponent(e, "MeshCollider")) {
            auto* mesh = static_cast<MeshCollider*>(engine_->GetComponent(e, "MeshCollider"));
            mesh->Update(tWrite->position, tWrite->rotation, tWrite->scale);
            activeColliders.push_back(mesh->aabb.get());
            aabbToEntity[mesh->aabb.get()] = e;
        }
    }

    // 3. Broadphase
    auto pairs = checkCollisions(activeColliders);

    // 4. Narrowphase Dispatch
    for (auto& pair : pairs) {
        Entity a = aabbToEntity[pair.first];
        Entity b = aabbToEntity[pair.second];

        // Skip Static vs Static
        auto* rbA = static_cast<RigidBody*>(engine_->GetComponent(a, "RigidBody"));
        auto* rbB = static_cast<RigidBody*>(engine_->GetComponent(b, "RigidBody"));

        if (rbA->isStatic && rbB->isStatic) continue;

        SolveNarrowPhase(a, b);
    }
}

void Physics3D::OnInit()
{

    engine_->AddEventListener("", [this](Event event)
        {
            this->octree = std::make_unique<Octree>(glm::vec3(0, 0, 0), 1000.0f);
            std::vector<AABB*> statics;

            for (Entity entity : mEntities)
            {
                if (!engine_->HasComponent(entity, "RigidBody")) continue;

                RigidBody* rbDyn = static_cast<RigidBody*>(engine_->GetComponent(entity, "RigidBody"));

                if (rbDyn->isStatic)
                {
                    if (engine_->HasComponent(entity, "BoxCollider")) {
                        auto* box = static_cast<BoxCollider*>(engine_->GetComponent(entity, "BoxCollider"));
                        statics.push_back(box->aabb.get());
                    }
                    if (engine_->HasComponent(entity, "CapsuleCollider")) {
                        auto* cap = static_cast<CapsuleCollider*>(engine_->GetComponent(entity, "CapsuleCollider"));
                        statics.push_back(cap->aabb.get());
                    }
                    if (engine_->HasComponent(entity, "MeshCollider")) {
                        auto* mesh = static_cast<MeshCollider*>(engine_->GetComponent(entity, "MeshCollider"));
                        statics.push_back(mesh->aabb.get());
                    }
                }
            }

            this->octree->BuildStatic(statics);
        });
}

void Physics3D::SolveNarrowPhase(Entity a, Entity b) {
    Manifold m = { a, b, glm::vec3(0), glm::vec3(0), 0, false };

    // --- Dispatch Logic ---
    bool hasBoxA = engine_->HasComponent(a, "BoxCollider");
    bool hasBoxB = engine_->HasComponent(b, "BoxCollider");

    bool hasCapA = engine_->HasComponent(a, "CapsuleCollider");
    bool hasMeshB = engine_->HasComponent(b, "MeshCollider");

    // Case 1: Box vs Box
    if (hasBoxA && hasBoxB) {
        m = CheckBoxVsBox(a, b);
    }
    // Case 2: Capsule vs Mesh (Player vs Level)
    else if (hasCapA && hasMeshB) {
        m = CheckCapsuleVsMesh(a, b);
    }
    // Add flip case: Mesh vs Capsule
    else if (hasMeshB && hasCapA) {
        m = CheckCapsuleVsMesh(b, a);
    }

    if (m.hasCollision) {
        ResolveCollision(m);
    }
}

Manifold Physics3D::CheckBoxVsBox(Entity a, Entity b) {
    Manifold m = { a, b, glm::vec3(0, 1, 0), glm::vec3(0), 0, false };

    auto* boxA = static_cast<BoxCollider*>(engine_->GetComponent(a, "BoxCollider"));
    auto* boxB = static_cast<BoxCollider*>(engine_->GetComponent(b, "BoxCollider"));

    glm::vec3 minA = boxA->aabb->GetMin();
    glm::vec3 maxA = boxA->aabb->GetMax();
    glm::vec3 minB = boxB->aabb->GetMin();
    glm::vec3 maxB = boxB->aabb->GetMax();

    // Calculate overlap on axes
    float xOv = std::min(maxA.x, maxB.x) - std::max(minA.x, minB.x);
    float yOv = std::min(maxA.y, maxB.y) - std::max(minA.y, minB.y);
    float zOv = std::min(maxA.z, maxB.z) - std::max(minA.z, minB.z);

    if (xOv > 0 && yOv > 0 && zOv > 0) {
        m.hasCollision = true;
        // Find Minimum Translation Vector
        if (xOv < yOv && xOv < zOv) {
            m.depth = xOv;
            m.normal = (minA.x < minB.x) ? glm::vec3(-1, 0, 0) : glm::vec3(1, 0, 0);
        }
        else if (yOv < zOv) {
            m.depth = yOv;
            m.normal = (minA.y < minB.y) ? glm::vec3(0, -1, 0) : glm::vec3(0, 1, 0);
        }
        else {
            m.depth = zOv;
            m.normal = (minA.z < minB.z) ? glm::vec3(0, 0, -1) : glm::vec3(0, 0, 1);
        }
        // --- NEW: Calculate Contact Point ---
        // 1. Get centers
        glm::vec3 centerA = (minA + maxA) * 0.5f;
        glm::vec3 centerB = (minB + maxB) * 0.5f;

        // 2. Determine which box is "A" in the manifold relative to normal
        // The normal points A -> B. We want the contact point on surface of A.
        glm::vec3 p = centerB;

        // 3. Clamp B's center onto A's AABB to find the closest point
        // This is a rough approximation but works well for non-rotated AABBs
        m.contactPoint.x = std::max(minA.x, std::min(p.x, maxA.x));
        m.contactPoint.y = std::max(minA.y, std::min(p.y, maxA.y));
        m.contactPoint.z = std::max(minA.z, std::min(p.z, maxA.z));

        // Slightly nudge contact point to be exactly on the edge if it's deeply inside
        // (Optional for stability, helps calculation of 'r' vector)
        if (m.normal.x != 0) m.contactPoint.x = (m.normal.x > 0) ? maxA.x : minA.x;
        if (m.normal.y != 0) m.contactPoint.y = (m.normal.y > 0) ? maxA.y : minA.y;
        if (m.normal.z != 0) m.contactPoint.z = (m.normal.z > 0) ? maxA.z : minA.z;
    }
    return m;
}

static inline glm::mat4 GetModelMatrix(Transform* transform) {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, transform->position);
    model *= glm::mat4_cast(transform->rotation);
    model = glm::scale(model, transform->scale);
    return model;
}

Manifold Physics3D::CheckCapsuleVsMesh(Entity capsuleEnt, Entity meshEnt) {
    Manifold m = { capsuleEnt, meshEnt, glm::vec3(0, 1, 0), glm::vec3(0), 0, false };

    auto* cap = static_cast<CapsuleCollider*>(engine_->GetComponent(capsuleEnt, "CapsuleCollider"));
    auto* capTrans = static_cast<Transform*>(engine_->GetComponent(capsuleEnt, "Transform"));

    auto* meshCol = static_cast<MeshCollider*>(engine_->GetComponent(meshEnt, "MeshCollider"));
    auto* meshTrans = static_cast<Transform*>(engine_->GetComponent(meshEnt, "Transform"));

    if (!meshCol->meshData) return m;

    // 1. Transform Capsule Center to Mesh Local Space
    // This allows us to check against raw mesh vertices without transforming them
    glm::mat4 worldToMesh = glm::inverse(GetModelMatrix(meshTrans));
    glm::vec3 capPosLocal = glm::vec3(worldToMesh * glm::vec4(capTrans->position, 1.0f));

    // For this example, we treat the bottom of the capsule as a sphere
    // Real implementation: Line Segment vs Triangle
    float radius = cap->radius;
    glm::vec3 sphereCenter = capPosLocal + glm::vec3(0, -cap->height * 0.5f + radius, 0);

    const auto& verts = meshCol->meshData->meshes[0].vertices;
    const auto& indices = meshCol->meshData->meshes[0].indices;

    float maxDepth = 0.0f;
    glm::vec3 avgNormal(0);

    // 2. Iterate Triangles (Optimization: Use Octree inside StaticMeshData in future)
    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3 v0 = verts[indices[i]].Position;
        glm::vec3 v1 = verts[indices[i + 1]].Position;
        glm::vec3 v2 = verts[indices[i + 2]].Position;

        // Simple Sphere vs Triangle
        glm::vec3 closest = ClosestPointTriangle(sphereCenter, v0, v1, v2);
        glm::vec3 diff = sphereCenter - closest;
        float distSq = glm::dot(diff, diff);

        if (distSq < radius * radius) {
            float dist = std::sqrt(distSq);
            float penetration = radius - dist;

            glm::vec3 triNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

            // Accumulate response
            if (penetration > maxDepth) {
                maxDepth = penetration;
                // Transform normal back to world space
                avgNormal = glm::normalize(glm::vec3(GetModelMatrix(meshTrans) * glm::vec4(triNormal, 0.0f)));
                m.hasCollision = true;
            }
        }
    }

    if (m.hasCollision) {
        m.depth = maxDepth;
        m.normal = -avgNormal; // Points out of mesh
    }

    return m;
}

bool Physics3D::GetCollisionManifold(const AABB& a, const AABB& b, Manifold& m) {
    // Calculate half extents
    glm::vec3 a_half = (a.GetMax() - a.GetMin()) * 0.5f;
    glm::vec3 b_half = (b.GetMax() - b.GetMin()) * 0.5f;

    // Calculate centers
    glm::vec3 a_center = (a.GetMin() + a.GetMax()) * 0.5f;
    glm::vec3 b_center = (b.GetMin() + b.GetMax()) * 0.5f;

    // Vector from A to B
    glm::vec3 n = b_center - a_center;

    // Calculate overlap on x, y, z
    float x_overlap = (a_half.x + b_half.x) - std::abs(n.x);
    float y_overlap = (a_half.y + b_half.y) - std::abs(n.y);
    float z_overlap = (a_half.z + b_half.z) - std::abs(n.z);

    // If any overlap is negative, they aren't colliding
    if (x_overlap < 0 || y_overlap < 0 || z_overlap < 0) return false;

    // Find the smallest overlap (axis of least penetration)
    // This assumes we want to push the object out the shortest way possible
    if (x_overlap < y_overlap && x_overlap < z_overlap) {
        m.depth = x_overlap;
        m.normal = glm::vec3(n.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
    }
    else if (y_overlap < z_overlap) {
        m.depth = y_overlap;
        m.normal = glm::vec3(0.0f, n.y > 0 ? 1.0f : -1.0f, 0.0f);
    }
    else {
        m.depth = z_overlap;
        m.normal = glm::vec3(0.0f, 0.0f, n.z > 0 ? 1.0f : -1.0f);
    }

    return true;
}

// Uses the same logic as WorldGenSystem to determine Y position
static float GetHeightInternal(float x, float z, float scale, float heightMultiplier) {
    float y = std::sin(x * scale) * std::cos(z * scale);
    y += std::sin(x * scale * 2.5f) * std::cos(z * scale * 2.5f) * 0.5f;
    return y * heightMultiplier;
}

// Uses the same logic as WorldGenSystem to calculate normal for accurate response
static glm::vec3 CalculateNormalInternal(float x, float z, float scale, float heightMultiplier) {
    float hL = GetHeightInternal(x - 1.0f, z, scale, heightMultiplier);
    float hR = GetHeightInternal(x + 1.0f, z, scale, heightMultiplier);
    float hD = GetHeightInternal(x, z - 1.0f, scale, heightMultiplier);
    float hU = GetHeightInternal(x, z + 1.0f, scale, heightMultiplier);

    glm::vec3 normal(hL - hR, 2.0f, hD - hU);
    return glm::normalize(normal);
}


void Physics3D::CheckDynamicVsHeightfield(Entity dynamicEntity, Entity terrainEntity) {
    // 1. Get components
    Transform* tDyn = static_cast<Transform*>(engine_->GetComponent(dynamicEntity, "Transform"));
    RigidBody* rbDyn = static_cast<RigidBody*>(engine_->GetComponent(dynamicEntity, "RigidBody"));
    BoxCollider* bcDyn = static_cast<BoxCollider*>(engine_->GetComponent(dynamicEntity, "BoxCollider"));

    // We need the heightfield component and its transform
    Transform* tHF = static_cast<Transform*>(engine_->GetComponent(terrainEntity, "Transform"));
    HeightfieldCollider* hf = static_cast<HeightfieldCollider*>(engine_->GetComponent(terrainEntity, "HeightfieldCollider"));

    // Ensure all components exist and the object is dynamic
    if (!tDyn || !rbDyn || !bcDyn || rbDyn->isStatic) return;

    // 2. Determine the world XZ position of the dynamic object's center
    glm::vec3 worldPos = tDyn->position;

    // 3. Convert world position to terrain-relative position
    // This is the XZ position relative to the terrain's origin (tHF->position)
    glm::vec3 relativePos = worldPos - tHF->position;

    // Quick boundary check: Is the object above the terrain's XZ area?
    if (std::abs(relativePos.x) > hf->halfWidth || std::abs(relativePos.z) > hf->halfDepth) {
        return;
    }

    // 4. Calculate Terrain Height at XZ (The Core Optimization)
    // We look up the height using the original generation math.
    float terrainY = GetHeightInternal(relativePos.x, relativePos.z, hf->scale, hf->heightMultiplier);
    // Add the terrain's world position offset
    terrainY += tHF->position.y;

    // 5. Get the lowest point of the dynamic object (AABB bottom)
    // We use the AABB's smallest Y offset relative to the dynamic entity's center.
    // NOTE: This assumes the dynamic object is still an AABB.
    float dynamicBottomY = tDyn->position.y + bcDyn->aabb->GetMin().y;

    // 6. Collision Check & Resolution
    if (dynamicBottomY < terrainY) {
        // Collision detected! The object is sinking into the terrain.

        float penetration = terrainY - dynamicBottomY;
        glm::vec3 normal = CalculateNormalInternal(relativePos.x, relativePos.z, hf->scale, hf->heightMultiplier);

        // Position Correction (Push the object out)
        tDyn->position.y += penetration;

        // Velocity Correction (Stop the falling motion)
        // Only apply force against the downward velocity component
        if (glm::dot(rbDyn->velocity, normal) < 0) {
            // Zero out the velocity along the normal (stopping downward motion)
            rbDyn->velocity -= glm::dot(rbDyn->velocity, normal) * normal;
        }

        // Apply friction to the horizontal velocity
        rbDyn->velocity.x *= (1.0f - rbDyn->friction);
        rbDyn->velocity.z *= (1.0f - rbDyn->friction);

        engine_->MarkEntityDirty(dynamicEntity, "Transform");
    }
}

void Physics3D::ResolveCollision(const Manifold& m) {
    auto* tA = static_cast<Transform*>(engine_->GetComponent(m.entityA, "Transform"));
    auto* rbA = static_cast<RigidBody*>(engine_->GetComponent(m.entityA, "RigidBody"));

    auto* tB = static_cast<Transform*>(engine_->GetComponent(m.entityB, "Transform"));
    auto* rbB = static_cast<RigidBody*>(engine_->GetComponent(m.entityB, "RigidBody"));

    // Early exit if both are static or missing
    if (!rbA && !rbB) return;

    // 1. Setup Mass and Inertia Data
    float invMassA = (rbA && !rbA->isStatic) ? rbA->inverseMass : 0.0f;
    float invMassB = (rbB && !rbB->isStatic) ? rbB->inverseMass : 0.0f;

    glm::mat3 invInertiaA = (rbA && !rbA->isStatic) ? rbA->inverseInertiaTensor : glm::mat3(0.0f);
    glm::mat3 invInertiaB = (rbB && !rbB->isStatic) ? rbB->inverseInertiaTensor : glm::mat3(0.0f);

    // 2. Position Correction (Prevent Sinking)
    // Move objects apart proportional to their inverse mass so they don't overlap
    const float percent = 1.f; // Penetration percentage to correct
    const float slop = 0.01f;   // Penetration allowance
    glm::vec3 correction = m.normal * std::max(m.depth - slop, 0.0f) / (invMassA + invMassB) * percent;

    if (invMassA > 0.0f) tA->position -= correction * invMassA;
    if (invMassB > 0.0f) tB->position += correction * invMassB;

    // 3. Calculate Relative Velocity
    // r vectors are vectors from Center of Mass to Contact Point
    glm::vec3 rA = m.contactPoint - tA->position;
    glm::vec3 rB = m.contactPoint - tB->position;

    glm::vec3 velA = (rbA) ? rbA->velocity + glm::cross(rbA->angularVelocity, rA) : glm::vec3(0);
    glm::vec3 velB = (rbB) ? rbB->velocity + glm::cross(rbB->angularVelocity, rB) : glm::vec3(0);
    glm::vec3 relativeVel = velB - velA;

    // 4. Calculate Impulse (Bounciness)
    float velAlongNormal = glm::dot(relativeVel, m.normal);

    // Do not resolve if velocities are separating
    if (velAlongNormal < 0) return;

    float e = std::min((rbA ? rbA->restitution : 0.0f), (rbB ? rbB->restitution : 0.0f));

    // Calculate the denominator of the impulse scalar equation
    glm::vec3 raxn = glm::cross(rA, m.normal);
    glm::vec3 rbxn = glm::cross(rB, m.normal);
    float invMassSum = invMassA + invMassB +
        glm::dot(raxn, invInertiaA * raxn) +
        glm::dot(rbxn, invInertiaB * rbxn);

    float j = -(1.0f + e) * velAlongNormal;
    j /= invMassSum;

    glm::vec3 impulse = m.normal * j;

    // Apply Linear and Angular Impulse
    if (invMassA > 0.0f) {
        rbA->velocity -= impulse * invMassA;
        rbA->angularVelocity -= invInertiaA * glm::cross(rA, impulse);
    }
    if (invMassB > 0.0f) {
        rbB->velocity += impulse * invMassB;
        rbB->angularVelocity += invInertiaB * glm::cross(rB, impulse);
    }

    // 5. Friction (Sliding)
    // Re-calculate relative velocity after normal impulse
    velA = (rbA) ? rbA->velocity + glm::cross(rbA->angularVelocity, rA) : glm::vec3(0);
    velB = (rbB) ? rbB->velocity + glm::cross(rbB->angularVelocity, rB) : glm::vec3(0);
    relativeVel = velB - velA;

    // Tangent vector is relative velocity projected onto the collision plane
    glm::vec3 tangent = relativeVel - (m.normal * glm::dot(relativeVel, m.normal));
    float tangentLen = glm::length(tangent);

    if (tangentLen > 0.0001f) {
        tangent /= tangentLen; // Normalize

        // Solve for tangent impulse magnitude (jt)
        glm::vec3 raxt = glm::cross(rA, tangent);
        glm::vec3 rbxt = glm::cross(rB, tangent);
        float invMassSumTan = invMassA + invMassB +
            glm::dot(raxt, invInertiaA * raxt) +
            glm::dot(rbxt, invInertiaB * rbxt);

        float jt = -glm::dot(relativeVel, tangent);
        jt /= invMassSumTan;

        // Coulomb's Law: Clamp friction to normal impulse magnitude
        float mu = std::sqrt((rbA ? rbA->friction : 0.5f) * (rbB ? rbB->friction : 0.5f));
        if (std::abs(jt) < j * mu) {
            impulse = tangent * jt;
        }
        else {
            impulse = tangent * -j * mu; // Dynamic friction
        }

        // Apply Friction Impulse
        if (invMassA > 0.0f) {
            rbA->velocity -= impulse * invMassA;
            rbA->angularVelocity -= invInertiaA * glm::cross(rA, impulse);
        }
        if (invMassB > 0.0f) {
            rbB->velocity += impulse * invMassB;
            rbB->angularVelocity += invInertiaB * glm::cross(rB, impulse);
        }
    }
}

void Physics3D::SyncStaticColliders() {
    for (Entity entity : mEntities) {
        if (processedStaticMeshes.count(entity)) continue;
        if (!engine_->HasComponent(entity, "StaticMesh") ||
            !engine_->HasComponent(entity, "BoxCollider")) continue;

        auto* mesh = static_cast<StaticMesh*>(engine_->GetComponent(entity, "StaticMesh"));
        auto* box = static_cast<BoxCollider*>(engine_->GetComponent(entity, "BoxCollider"));

        if (mesh && mesh->MeshResource && mesh->MeshResource->cpuMesh && !box->HasFittedToMesh) {
            box->FitToMesh(mesh->MeshResource->cpuMesh->aabbMin, mesh->MeshResource->cpuMesh->aabbMax);
            processedStaticMeshes.insert(entity);
        }
    }
}

void Physics3D::AddForceToEntity(Entity entity, glm::vec3 force) {
    if (engine_->HasComponent(entity, "RigidBody")) {
        auto* rb = static_cast<RigidBody*>(engine_->GetComponent(entity, "RigidBody"));
        if (!rb->isStatic) rb->acceleration += force / rb->mass;
    }
}

extern "C" {
    REFLECTION_API System* CreateSystem() { return new Physics3D(); }
}