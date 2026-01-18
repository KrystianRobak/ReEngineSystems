#include "Physics3D.h"
#include "Components/Transform.h"
#include "Components/RigidBody.h"   
#include "Components/BoxCollider.h" 
#include "Components/StaticMesh.h"
#include "Octree.h"       
#include <algorithm>
#include <iostream>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <CapsuleCollider.h>
#include <MeshCollider.h>
#include <HeightfieldCollider.h>
#include <queue>
#include <unordered_set>

extern "C" {
    REFLECTION_API System* CreateSystem() { return new Physics3D(); }
}

void Physics3D::OnInit() {
    octree = std::make_unique<Octree>(glm::vec3(0.0f), 2000.0f);
    staticsInitialized = false;
    contactCache.clear();
    islands.clear();
}

void Physics3D::Cleanup() {
    octree.reset();
    contactCache.clear();
    islands.clear();
}

void Physics3D::Update(float dt) {
    if (!engine_ || !octree) return;

    // Clamp dt to prevent instability
    dt = std::min(dt, 0.033f); // Max 33ms (30 FPS minimum)

    // 1. Auto-Fit Colliders (Essential for matching visuals) - Only runs once per mesh
    FitCollidersToMeshes();

    // 2. Setup
    aabbToEntityMap.clear();
    currentFrameDynamics.clear();
    if (!staticsInitialized) currentFrameStatics.clear();

    // 3. Gravity & Force Application (skips sleeping objects)
    ApplyGravityAndForces(dt);

    // 4. Build Static Octree (One-time cost)
    if (!staticsInitialized && !currentFrameStatics.empty()) {
        octree->BuildStatic(currentFrameStatics);
        staticsInitialized = true;
    }

    // 5. Broad Phase (Octree) + Narrow Phase (SAT)
    FindCollisions();

    // 6. Build Simulation Islands
    BuildIslands();

    // 7. Solve Each Island Independently
    for (auto& island : islands) {
        if (island.isSleeping) continue; // Skip sleeping islands!

        // Iterative Constraint Solver per island
        for (int i = 0; i < SOLVER_ITERATIONS; ++i) {
            ResolveIslandCollisions(island, dt);
        }
    }

    // 8. Velocity Integration (skips sleeping objects)
    IntegratePositions(dt);

    // 9. Update Island Sleep States
    UpdateIslandSleeping(dt);

    // 10. Clean old cached contacts
    CleanContactCache();
}

void Physics3D::FitCollidersToMeshes() {
    for (Entity entity : mEntities) {
        if (!engine_->HasComponent(entity, "BoxCollider")) continue;
        if (!engine_->HasComponent(entity, "StaticMesh")) continue;

        auto* box = (BoxCollider*)engine_->GetComponent(entity, "BoxCollider");
        auto* mesh = (StaticMesh*)engine_->GetComponent(entity, "StaticMesh");

        if (!box->HasFittedToMesh && mesh->MeshResource && mesh->MeshResource->cpuMesh) {
            box->FitToMesh(mesh->MeshResource->cpuMesh->aabbMin, mesh->MeshResource->cpuMesh->aabbMax);
            Transform* tx = (Transform*)engine_->GetComponent(entity, "Transform");
            if (tx) SyncColliderTransform(entity, tx);
        }
    }
}

OBB Physics3D::BuildOBB(Entity entity) {
    Transform* tx = (Transform*)engine_->GetComponent(entity, "Transform");
    BoxCollider* bc = (BoxCollider*)engine_->GetComponent(entity, "BoxCollider");

    OBB obb;
    glm::vec3 size = bc ? bc->size : glm::vec3(1.0f);
    glm::vec3 offset = bc ? bc->offset : glm::vec3(0.0f);

    obb.center = tx->position + (tx->rotation * (offset * tx->scale));
    glm::mat3 rotMat = glm::toMat3(tx->rotation);
    obb.axes[0] = rotMat[0]; obb.axes[1] = rotMat[1]; obb.axes[2] = rotMat[2];
    obb.halfExtents = (size * tx->scale) * 0.5f;
    return obb;
}

glm::mat3 Physics3D::ComputeInverseInertiaTensor(float mass, const glm::vec3& halfExtents, const glm::quat& rotation) {
    if (mass <= 0.0f) return glm::mat3(0.0f);
    float w = halfExtents.x * 2.0f;
    float h = halfExtents.y * 2.0f;
    float d = halfExtents.z * 2.0f;

    glm::vec3 I;
    I.x = (1.0f / 12.0f) * mass * (h * h + d * d);
    I.y = (1.0f / 12.0f) * mass * (w * w + d * d);
    I.z = (1.0f / 12.0f) * mass * (w * w + h * h);

    // Prevent division by zero for thin objects
    I.x = std::max(I.x, 0.001f);
    I.y = std::max(I.y, 0.001f);
    I.z = std::max(I.z, 0.001f);

    glm::mat3 I_invLocal(0.0f);
    I_invLocal[0][0] = 1.0f / I.x;
    I_invLocal[1][1] = 1.0f / I.y;
    I_invLocal[2][2] = 1.0f / I.z;

    glm::mat3 R = glm::toMat3(rotation);
    return R * I_invLocal * glm::transpose(R);
}

// Helper to get all 8 corners of an OBB
void GetOBBCorners(const OBB& obb, glm::vec3 corners[8]) {
    glm::vec3 axisX = obb.axes[0] * obb.halfExtents.x;
    glm::vec3 axisY = obb.axes[1] * obb.halfExtents.y;
    glm::vec3 axisZ = obb.axes[2] * obb.halfExtents.z;

    corners[0] = obb.center - axisX - axisY - axisZ;
    corners[1] = obb.center + axisX - axisY - axisZ;
    corners[2] = obb.center + axisX + axisY - axisZ;
    corners[3] = obb.center - axisX + axisY - axisZ;
    corners[4] = obb.center - axisX - axisY + axisZ;
    corners[5] = obb.center + axisX - axisY + axisZ;
    corners[6] = obb.center + axisX + axisY + axisZ;
    corners[7] = obb.center - axisX + axisY + axisZ;
}

// Helper to clip a point onto an OBB
glm::vec3 ClosestPointOnOBB(const glm::vec3& point, const OBB& obb) {
    glm::vec3 d = point - obb.center;
    glm::vec3 result = obb.center;

    for (int i = 0; i < 3; i++) {
        float dist = glm::dot(d, obb.axes[i]);
        dist = glm::clamp(dist, -obb.halfExtents[i], obb.halfExtents[i]);
        result += obb.axes[i] * dist;
    }

    return result;
}

bool Physics3D::CheckCollisionSAT(Entity entityA, Entity entityB, CollisionManifold& outManifold) {
    OBB a = BuildOBB(entityA);
    OBB b = BuildOBB(entityB);
    glm::vec3 centerDist = b.center - a.center;

    // 15 Axes Check (Standard SAT)
    glm::vec3 axes[15];
    int axisCount = 0;
    for (int i = 0; i < 3; i++) axes[axisCount++] = a.axes[i];
    for (int i = 0; i < 3; i++) axes[axisCount++] = b.axes[i];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            glm::vec3 cross = glm::cross(a.axes[i], b.axes[j]);
            if (glm::length(cross) > 0.001f) {
                axes[axisCount++] = glm::normalize(cross);
            }
        }
    }

    float minPen = FLT_MAX;
    glm::vec3 bestAxis(0, 1, 0);
    int bestAxisIndex = -1;

    for (int i = 0; i < axisCount; i++) {
        glm::vec3 axis = axes[i];
        float rA = glm::abs(glm::dot(a.axes[0], axis) * a.halfExtents.x) +
            glm::abs(glm::dot(a.axes[1], axis) * a.halfExtents.y) +
            glm::abs(glm::dot(a.axes[2], axis) * a.halfExtents.z);
        float rB = glm::abs(glm::dot(b.axes[0], axis) * b.halfExtents.x) +
            glm::abs(glm::dot(b.axes[1], axis) * b.halfExtents.y) +
            glm::abs(glm::dot(b.axes[2], axis) * b.halfExtents.z);
        float dist = glm::abs(glm::dot(centerDist, axis));
        float pen = (rA + rB) - dist;

        if (pen <= 0) return false; // Gap found - no collision

        if (pen < minPen) {
            minPen = pen;
            bestAxis = axis;
            bestAxisIndex = i;
        }
    }

    // Ensure normal points from A to B
    if (glm::dot(bestAxis, centerDist) < 0) {
        bestAxis = -bestAxis;
    }

    outManifold.entityA = entityA;
    outManifold.entityB = entityB;
    outManifold.normal = bestAxis;

    // --- IMPROVED MANIFOLD GENERATION ---
    bool isFaceCollision = bestAxisIndex < 6;

    if (isFaceCollision) {
        glm::vec3 cornersB[8];
        GetOBBCorners(b, cornersB);

        for (int i = 0; i < 8; i++) {
            float distanceAlongNormal = glm::dot(cornersB[i] - a.center, bestAxis);

            float aExtent = glm::abs(glm::dot(a.axes[0], bestAxis) * a.halfExtents.x) +
                glm::abs(glm::dot(a.axes[1], bestAxis) * a.halfExtents.y) +
                glm::abs(glm::dot(a.axes[2], bestAxis) * a.halfExtents.z);

            float penetration = aExtent - distanceAlongNormal;

            if (penetration > -0.01f) {
                glm::vec3 contactPoint = ClosestPointOnOBB(cornersB[i], a);
                outManifold.contacts.push_back({ contactPoint, penetration });
            }
        }

        if (outManifold.contacts.size() < 2) {
            glm::vec3 cornersA[8];
            GetOBBCorners(a, cornersA);

            for (int i = 0; i < 8; i++) {
                float distanceAlongNormal = glm::dot(cornersA[i] - b.center, -bestAxis);
                float bExtent = glm::abs(glm::dot(b.axes[0], -bestAxis) * b.halfExtents.x) +
                    glm::abs(glm::dot(b.axes[1], -bestAxis) * b.halfExtents.y) +
                    glm::abs(glm::dot(b.axes[2], -bestAxis) * b.halfExtents.z);

                float penetration = bExtent - distanceAlongNormal;

                if (penetration > -0.01f) {
                    glm::vec3 contactPoint = ClosestPointOnOBB(cornersA[i], b);
                    outManifold.contacts.push_back({ contactPoint, penetration });
                }
            }
        }
    }
    else {
        glm::vec3 closestOnA = ClosestPointOnOBB(b.center, a);
        glm::vec3 closestOnB = ClosestPointOnOBB(a.center, b);
        glm::vec3 contactPoint = (closestOnA + closestOnB) * 0.5f;
        outManifold.contacts.push_back({ contactPoint, minPen });
    }

    if (outManifold.contacts.size() > 4) {
        std::sort(outManifold.contacts.begin(), outManifold.contacts.end(),
            [](const ContactPoint& a, const ContactPoint& b) {
                return a.penetration > b.penetration;
            });
        outManifold.contacts.resize(4);
    }

    if (outManifold.contacts.empty()) {
        glm::vec3 contactPoint = a.center + bestAxis * (glm::length(a.halfExtents));
        outManifold.contacts.push_back({ contactPoint, minPen });
    }

    return true;
}

uint64_t Physics3D::MakePairKey(Entity a, Entity b) {
    if (a > b) std::swap(a, b);
    return ((uint64_t)a << 32) | (uint64_t)b;
}

void Physics3D::ResolveIslandCollisions(SimulationIsland& island, float dt) {
    const float RESTITUTION_THRESHOLD = 1.0f; // Increased - only bounce on harder impacts
    const float FRICTION_COEFF = 0.6f;
    const float BAUMGARTE_COEFF = 0.3f;
    const float SLOP = 0.005f;

    // Only process manifolds that belong to this island
    for (const auto& m : island.manifolds) {
        RigidBody* rbA = (RigidBody*)engine_->GetComponent(m.entityA, "RigidBody");
        RigidBody* rbB = (RigidBody*)engine_->GetComponent(m.entityB, "RigidBody");
        Transform* txA = (Transform*)engine_->GetComponentForWrite(m.entityA, "Transform");
        Transform* txB = (Transform*)engine_->GetComponentForWrite(m.entityB, "Transform");

        float invMassA = (rbA && !rbA->isStatic) ? (1.0f / rbA->mass) : 0.0f;
        float invMassB = (rbB && !rbB->isStatic) ? (1.0f / rbB->mass) : 0.0f;
        float totalInvMass = invMassA + invMassB;
        if (totalInvMass == 0.0f) continue;

        OBB obbA = BuildOBB(m.entityA);
        OBB obbB = BuildOBB(m.entityB);
        glm::mat3 iA = (rbA && !rbA->isStatic) ? ComputeInverseInertiaTensor(rbA->mass, obbA.halfExtents, txA->rotation) : glm::mat3(0.0f);
        glm::mat3 iB = (rbB && !rbB->isStatic) ? ComputeInverseInertiaTensor(rbB->mass, obbB.halfExtents, txB->rotation) : glm::mat3(0.0f);

        // Get or create cached contact
        uint64_t pairKey = MakePairKey(m.entityA, m.entityB);
        bool isCachedContact = contactCache.find(pairKey) != contactCache.end();
        PersistentContact& cached = contactCache[pairKey];

        // Check if this is a "similar" contact (same normal direction)
        bool isMatchingContact = isCachedContact && glm::dot(cached.normal, m.normal) > 0.95f;

        // Update cache
        if (!isMatchingContact) {
            // Reset accumulated impulses for new/different collision
            for (int k = 0; k < 4; k++) {
                cached.accumulatedNormalImpulse[k] = 0.0f;
                cached.accumulatedTangentImpulse[k] = 0.0f;
            }
        }

        cached.entityA = m.entityA;
        cached.entityB = m.entityB;
        cached.normal = m.normal;
        cached.framesSinceUpdate = 0;

        // Process contacts
        for (size_t i = 0; i < m.contacts.size() && i < 4; i++) {
            glm::vec3 rA = m.contacts[i].position - txA->position;
            glm::vec3 rB = m.contacts[i].position - txB->position;

            glm::vec3 velA = (rbA && !rbA->isStatic) ? rbA->velocity + glm::cross(rbA->angularVelocity, rA) : glm::vec3(0);
            glm::vec3 velB = (rbB && !rbB->isStatic) ? rbB->velocity + glm::cross(rbB->angularVelocity, rB) : glm::vec3(0);
            glm::vec3 relVel = velB - velA;
            float velAlongNormal = glm::dot(relVel, m.normal);

            // WARM START: Only apply if contact is persistent and similar
            if (isMatchingContact && cached.accumulatedNormalImpulse[i] > 0.01f) {
                glm::vec3 warmImpulse = cached.accumulatedNormalImpulse[i] * m.normal * 0.5f; // Reduced from 0.8

                if (rbA && !rbA->isStatic) {
                    rbA->velocity -= warmImpulse * invMassA;
                    rbA->angularVelocity -= iA * glm::cross(rA, warmImpulse);
                }
                if (rbB && !rbB->isStatic) {
                    rbB->velocity += warmImpulse * invMassB;
                    rbB->angularVelocity += iB * glm::cross(rB, warmImpulse);
                }

                // Recalculate velocity after warm start
                velA = (rbA && !rbA->isStatic) ? rbA->velocity + glm::cross(rbA->angularVelocity, rA) : glm::vec3(0);
                velB = (rbB && !rbB->isStatic) ? rbB->velocity + glm::cross(rbB->angularVelocity, rB) : glm::vec3(0);
                relVel = velB - velA;
                velAlongNormal = glm::dot(relVel, m.normal);
            }

            if (velAlongNormal < 0) {
                glm::vec3 raxn = glm::cross(rA, m.normal);
                glm::vec3 rbxn = glm::cross(rB, m.normal);
                float rotTerm = glm::dot(raxn, iA * raxn) + glm::dot(rbxn, iB * rbxn);

                // Dynamic restitution: only bounce on high-speed impacts
                float restitution = 0.0f;
                if (glm::abs(velAlongNormal) > RESTITUTION_THRESHOLD) {
                    restitution = 0.2f; // Reduced from 0.3
                }

                float j = -(1.0f + restitution) * velAlongNormal;
                j /= (totalInvMass + rotTerm);

                glm::vec3 impulse = j * m.normal;

                // Friction
                glm::vec3 tangent = relVel - (velAlongNormal * m.normal);
                float tangentLength = glm::length(tangent);

                if (tangentLength > 0.001f) {
                    tangent = tangent / tangentLength;

                    glm::vec3 raxt = glm::cross(rA, tangent);
                    glm::vec3 rbxt = glm::cross(rB, tangent);
                    float rotTermTangent = glm::dot(raxt, iA * raxt) + glm::dot(rbxt, iB * rbxt);

                    float jt = -glm::dot(relVel, tangent);
                    jt /= (totalInvMass + rotTermTangent);

                    // Warm start tangent: only if this is a matching persistent contact
                    if (isMatchingContact) {
                        jt += cached.accumulatedTangentImpulse[i] * 0.5f; // Reduced from 0.8
                    }

                    float maxFriction = FRICTION_COEFF * glm::abs(j);
                    jt = glm::clamp(jt, -maxFriction, maxFriction);

                    impulse += jt * tangent;

                    // Cache tangent impulse
                    cached.accumulatedTangentImpulse[i] = jt;
                }

                if (rbA && !rbA->isStatic) {
                    rbA->velocity -= impulse * invMassA;
                    rbA->angularVelocity -= iA * glm::cross(rA, impulse);
                }
                if (rbB && !rbB->isStatic) {
                    rbB->velocity += impulse * invMassB;
                    rbB->angularVelocity += iB * glm::cross(rB, impulse);
                }

                // Cache normal impulse for next frame
                cached.accumulatedNormalImpulse[i] = j;
            }

            // Positional correction
            float depth = std::max(m.contacts[i].penetration - SLOP, 0.0f);
            if (depth > 0.0f) {
                glm::vec3 correction = (depth / totalInvMass) * BAUMGARTE_COEFF * m.normal;

                if (rbA && !rbA->isStatic) {
                    txA->position -= correction * invMassA;
                }
                if (rbB && !rbB->isStatic) {
                    txB->position += correction * invMassB;
                }
            }
        }

        SyncColliderTransform(m.entityA, txA);
        SyncColliderTransform(m.entityB, txB);
    }
}

void Physics3D::BuildIslands() {
    islands.clear();

    std::unordered_set<Entity> visited;
    std::unordered_map<Entity, std::vector<Entity>> graph;

    // Build connectivity graph from manifolds
    for (const auto& m : manifolds) {
        graph[m.entityA].push_back(m.entityB);
        graph[m.entityB].push_back(m.entityA);
    }

    // BFS to find connected components
    for (const auto& m : manifolds) {
        for (Entity startEntity : {m.entityA, m.entityB}) {
            if (visited.count(startEntity)) continue;

            SimulationIsland island;
            std::queue<Entity> toVisit;
            toVisit.push(startEntity);
            visited.insert(startEntity);

            while (!toVisit.empty()) {
                Entity current = toVisit.front();
                toVisit.pop();

                island.entities.push_back(current);

                for (Entity neighbor : graph[current]) {
                    if (!visited.count(neighbor)) {
                        visited.insert(neighbor);
                        toVisit.push(neighbor);
                    }
                }
            }

            // Gather manifolds for this island
            for (const auto& manifold : manifolds) {
                if (std::find(island.entities.begin(), island.entities.end(), manifold.entityA) != island.entities.end() ||
                    std::find(island.entities.begin(), island.entities.end(), manifold.entityB) != island.entities.end()) {
                    island.manifolds.push_back(manifold);
                }
            }

            island.isSleeping = false;
            island.sleepTimer = 0.0f;
            islands.push_back(island);
        }
    }
}

void Physics3D::UpdateIslandSleeping(float dt) {
    const float SLEEP_LINEAR_THRESHOLD = 0.1f;
    const float SLEEP_ANGULAR_THRESHOLD = 0.1f;
    const float SLEEP_TIME_REQUIRED = 0.5f; // Must be stable for 0.5 seconds

    for (auto& island : islands) {
        bool islandStable = true;

        // Check if all entities in island are slow
        for (Entity entity : island.entities) {
            RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
            if (!rb || rb->isStatic) continue;

            float linearSpeed = glm::length(rb->velocity);
            float angularSpeed = glm::length(rb->angularVelocity);

            if (linearSpeed > SLEEP_LINEAR_THRESHOLD || angularSpeed > SLEEP_ANGULAR_THRESHOLD) {
                islandStable = false;
                break;
            }
        }

        if (islandStable) {
            island.sleepTimer += dt;

            if (island.sleepTimer >= SLEEP_TIME_REQUIRED && !island.isSleeping) {
                // Put island to sleep
                island.isSleeping = true;

                // Zero out velocities
                for (Entity entity : island.entities) {
                    RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
                    if (rb && !rb->isStatic) {
                        rb->velocity = glm::vec3(0.0f);
                        rb->angularVelocity = glm::vec3(0.0f);
                    }
                }
            }
        }
        else {
            island.sleepTimer = 0.0f;
            island.isSleeping = false;
        }
    }
}

void Physics3D::WakeIsland(Entity entity) {
    // Find which island this entity belongs to and wake it
    for (auto& island : islands) {
        if (std::find(island.entities.begin(), island.entities.end(), entity) != island.entities.end()) {
            island.isSleeping = false;
            island.sleepTimer = 0.0f;
            return;
        }
    }
}

void Physics3D::CleanContactCache() {
    // Remove contacts that haven't been updated in 5 frames
    auto it = contactCache.begin();
    while (it != contactCache.end()) {
        it->second.framesSinceUpdate++;
        if (it->second.framesSinceUpdate > 5) {
            it = contactCache.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Physics3D::FindCollisions() {
    potentialCollisions.clear();
    manifolds.clear();
    octree->Update(currentFrameDynamics, potentialCollisions);

    for (const auto& pair : potentialCollisions) {
        Entity entityA = aabbToEntityMap[pair.first];
        Entity entityB = aabbToEntityMap[pair.second];
        if (entityA == entityB) continue;

        // Wake up islands when new collision detected
        WakeIsland(entityA);
        WakeIsland(entityB);

        CollisionManifold manifold;
        if (CheckCollisionSAT(entityA, entityB, manifold)) {
            manifolds.push_back(manifold);
        }
    }
}

void Physics3D::ApplyGravityAndForces(float dt) {
    for (Entity entity : mEntities) {
        auto* tx = (Transform*)engine_->GetComponentForWrite(entity, "Transform");
        if (!tx) continue;

        RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
        AABB* aabb = GetEntityAABB(entity);
        if (!aabb) continue;

        aabbToEntityMap[aabb] = entity;

        if (rb && !rb->isStatic) {
            // Check if entity is in a sleeping island
            bool isSleeping = false;
            for (const auto& island : islands) {
                if (island.isSleeping &&
                    std::find(island.entities.begin(), island.entities.end(), entity) != island.entities.end()) {
                    isSleeping = true;
                    break;
                }
            }

            if (!isSleeping) {
                rb->velocity += (gravity + rb->acceleration) * dt;
                rb->acceleration = glm::vec3(0.0f);
                SyncColliderTransform(entity, tx);
                currentFrameDynamics.push_back(aabb);
                engine_->MarkEntityDirty(entity, "Transform");
            }
        }
        else if (!staticsInitialized) {
            SyncColliderTransform(entity, tx);
            currentFrameStatics.push_back(aabb);
        }
    }
}

void Physics3D::IntegratePositions(float dt) {
    const float SLEEP_LINEAR_THRESHOLD = 0.05f;
    const float SLEEP_ANGULAR_THRESHOLD = 0.05f;
    const float LINEAR_DAMPING = 0.98f;
    const float ANGULAR_DAMPING = 0.92f;

    for (Entity entity : mEntities) {
        if (!engine_->HasComponent(entity, "RigidBody")) continue;

        RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
        if (rb->isStatic) continue;

        // Skip sleeping entities
        bool isSleeping = false;
        for (const auto& island : islands) {
            if (island.isSleeping &&
                std::find(island.entities.begin(), island.entities.end(), entity) != island.entities.end()) {
                isSleeping = true;
                break;
            }
        }

        if (isSleeping) continue;

        Transform* tx = (Transform*)engine_->GetComponentForWrite(entity, "Transform");

        float linearSpeed = glm::length(rb->velocity);
        float angularSpeed = glm::length(rb->angularVelocity);

        if (linearSpeed < SLEEP_LINEAR_THRESHOLD && angularSpeed < SLEEP_ANGULAR_THRESHOLD) {
            rb->velocity = glm::vec3(0.0f);
            rb->angularVelocity = glm::vec3(0.0f);
        }
        else {
            tx->position += rb->velocity * dt;

            rb->velocity *= LINEAR_DAMPING;
            rb->angularVelocity *= ANGULAR_DAMPING;

            if (angularSpeed > 0.001f) {
                float angleStep = angularSpeed * dt;
                glm::vec3 axis = rb->angularVelocity / angularSpeed;
                glm::quat deltaRotation = glm::angleAxis(angleStep, axis);
                tx->rotation = glm::normalize(deltaRotation * tx->rotation);
            }

            SyncColliderTransform(entity, tx);
        }
    }
}

AABB* Physics3D::GetEntityAABB(Entity entity) {
    if (engine_->HasComponent(entity, "BoxCollider"))
        return ((BoxCollider*)engine_->GetComponent(entity, "BoxCollider"))->aabb.get();
    if (engine_->HasComponent(entity, "CapsuleCollider"))
        return ((CapsuleCollider*)engine_->GetComponent(entity, "CapsuleCollider"))->aabb.get();
    if (engine_->HasComponent(entity, "MeshCollider"))
        return ((MeshCollider*)engine_->GetComponent(entity, "MeshCollider"))->aabb.get();
    if (engine_->HasComponent(entity, "HeightfieldCollider"))
        return ((HeightfieldCollider*)engine_->GetComponent(entity, "HeightfieldCollider"))->aabb.get();
    return nullptr;
}

void Physics3D::SyncColliderTransform(Entity entity, Transform* transform) {
    if (engine_->HasComponent(entity, "BoxCollider"))
        ((BoxCollider*)engine_->GetComponent(entity, "BoxCollider"))->Update(transform->position, transform->rotation, transform->scale);
    else if (engine_->HasComponent(entity, "CapsuleCollider"))
        ((CapsuleCollider*)engine_->GetComponent(entity, "CapsuleCollider"))->Update(transform->position, transform->rotation, transform->scale);
    else if (engine_->HasComponent(entity, "MeshCollider"))
        ((MeshCollider*)engine_->GetComponent(entity, "MeshCollider"))->Update(transform->position, transform->rotation, transform->scale);
    else if (engine_->HasComponent(entity, "HeightfieldCollider"))
        ((HeightfieldCollider*)engine_->GetComponent(entity, "HeightfieldCollider"))->Update(transform->position);
}

void Physics3D::ResolveHeightfieldCollision(Entity dynamicEntity, Entity terrainEntity) {
    auto* rb = (RigidBody*)engine_->GetComponent(dynamicEntity, "RigidBody");
    auto* tx = (Transform*)engine_->GetComponentForWrite(dynamicEntity, "Transform");
    auto* hf = (HeightfieldCollider*)engine_->GetComponent(terrainEntity, "HeightfieldCollider");
    auto* terrainTx = (Transform*)engine_->GetComponent(terrainEntity, "Transform");

    if (!rb || !tx || !hf) return;

    float relX = tx->position.x - terrainTx->position.x;
    float relZ = tx->position.z - terrainTx->position.z;

    if (relX < -hf->halfWidth || relX > hf->halfWidth ||
        relZ < -hf->halfDepth || relZ > hf->halfDepth) return;

    float terrainHeight = hf->GetHeightAt(relX, relZ) + terrainTx->position.y;
    float objectBottom = tx->position.y - (0.5f * tx->scale.y);

    if (objectBottom < terrainHeight) {
        float penetration = terrainHeight - objectBottom;
        tx->position.y += penetration;

        if (rb->velocity.y < 0) rb->velocity.y = 0;

        rb->velocity.x *= 0.95f;
        rb->velocity.z *= 0.95f;

        SyncColliderTransform(dynamicEntity, tx);

        // Wake island on terrain collision
        WakeIsland(dynamicEntity);
    }
}

void Physics3D::AddForceToEntity(Entity entity, glm::vec3 force) {
    if (engine_->HasComponent(entity, "RigidBody")) {
        RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
        if (rb && !rb->isStatic) {
            rb->acceleration += force / rb->mass;
            // Wake the island when force is applied
            WakeIsland(entity);
        }
    }
}

// ===========================
// PHYSICS QUERY API
// ===========================

bool Physics3D::RaycastSingle(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
    RaycastHit& outHit, const PhysicsQueryParams& params) {

    glm::vec3 rayEnd = origin + direction * maxDistance;
    float closestDist = maxDistance;
    bool hitFound = false;

    for (Entity entity : mEntities) {
        // Filter by channel
        if (!params.channels.empty()) {
            bool matchesChannel = false;
            // You'd need to add a "Channel" component or tag system
            // For now, we'll skip this check
        }

        // Skip if entity is in ignore list
        if (std::find(params.ignoreEntities.begin(), params.ignoreEntities.end(), entity) != params.ignoreEntities.end()) {
            continue;
        }

        // Check collision
        OBB obb = BuildOBB(entity);
        float tMin, tMax;

        if (RayOBBIntersection(origin, direction, obb, tMin, tMax)) {
            if (tMin >= 0 && tMin < closestDist) {
                closestDist = tMin;
                hitFound = true;

                outHit.entity = entity;
                outHit.position = origin + direction * tMin;
                outHit.distance = tMin;

                // Calculate normal (approximate - point on OBB closest to hit)
                glm::vec3 localHit = outHit.position - obb.center;
                outHit.normal = glm::vec3(0, 1, 0); // Default

                // Find which face was hit
                float maxProj = -FLT_MAX;
                for (int i = 0; i < 3; i++) {
                    float proj = glm::dot(localHit, obb.axes[i]) / obb.halfExtents[i];
                    if (glm::abs(proj) > glm::abs(maxProj)) {
                        maxProj = proj;
                        outHit.normal = obb.axes[i] * glm::sign(proj);
                    }
                }
            }
        }
    }

    return hitFound;
}

std::vector<RaycastHit> Physics3D::RaycastAll(const glm::vec3& origin, const glm::vec3& direction,
    float maxDistance, const PhysicsQueryParams& params) {

    std::vector<RaycastHit> hits;

    for (Entity entity : mEntities) {
        if (std::find(params.ignoreEntities.begin(), params.ignoreEntities.end(), entity) != params.ignoreEntities.end()) {
            continue;
        }

        OBB obb = BuildOBB(entity);
        float tMin, tMax;

        if (RayOBBIntersection(origin, direction, obb, tMin, tMax)) {
            if (tMin >= 0 && tMin <= maxDistance) {
                RaycastHit hit;
                hit.entity = entity;
                hit.position = origin + direction * tMin;
                hit.distance = tMin;

                glm::vec3 localHit = hit.position - obb.center;
                hit.normal = glm::vec3(0, 1, 0);

                float maxProj = -FLT_MAX;
                for (int i = 0; i < 3; i++) {
                    float proj = glm::dot(localHit, obb.axes[i]) / obb.halfExtents[i];
                    if (glm::abs(proj) > glm::abs(maxProj)) {
                        maxProj = proj;
                        hit.normal = obb.axes[i] * glm::sign(proj);
                    }
                }

                hits.push_back(hit);
            }
        }
    }

    // Sort by distance
    std::sort(hits.begin(), hits.end(), [](const RaycastHit& a, const RaycastHit& b) {
        return a.distance < b.distance;
        });

    return hits;
}

bool Physics3D::SphereCastSingle(const glm::vec3& origin, float radius, const glm::vec3& direction,
    float maxDistance, RaycastHit& outHit, const PhysicsQueryParams& params) {

    float closestDist = maxDistance;
    bool hitFound = false;

    for (Entity entity : mEntities) {
        if (std::find(params.ignoreEntities.begin(), params.ignoreEntities.end(), entity) != params.ignoreEntities.end()) {
            continue;
        }

        OBB obb = BuildOBB(entity);

        // Expand OBB by sphere radius
        OBB expandedOBB = obb;
        expandedOBB.halfExtents += glm::vec3(radius);

        float tMin, tMax;
        if (RayOBBIntersection(origin, direction, expandedOBB, tMin, tMax)) {
            if (tMin >= 0 && tMin < closestDist) {
                closestDist = tMin;
                hitFound = true;

                outHit.entity = entity;
                outHit.position = origin + direction * tMin;
                outHit.distance = tMin;

                // Normal points from OBB to sphere center at impact
                glm::vec3 sphereCenter = origin + direction * tMin;
                glm::vec3 closestPoint = ClosestPointOnOBB(sphereCenter, obb);
                outHit.normal = glm::normalize(sphereCenter - closestPoint);
            }
        }
    }

    return hitFound;
}

std::vector<RaycastHit> Physics3D::SphereCastAll(const glm::vec3& origin, float radius,
    const glm::vec3& direction, float maxDistance, const PhysicsQueryParams& params) {

    std::vector<RaycastHit> hits;

    for (Entity entity : mEntities) {
        if (std::find(params.ignoreEntities.begin(), params.ignoreEntities.end(), entity) != params.ignoreEntities.end()) {
            continue;
        }

        OBB obb = BuildOBB(entity);
        OBB expandedOBB = obb;
        expandedOBB.halfExtents += glm::vec3(radius);

        float tMin, tMax;
        if (RayOBBIntersection(origin, direction, expandedOBB, tMin, tMax)) {
            if (tMin >= 0 && tMin <= maxDistance) {
                RaycastHit hit;
                hit.entity = entity;
                hit.position = origin + direction * tMin;
                hit.distance = tMin;

                glm::vec3 sphereCenter = origin + direction * tMin;
                glm::vec3 closestPoint = ClosestPointOnOBB(sphereCenter, obb);
                hit.normal = glm::normalize(sphereCenter - closestPoint);

                hits.push_back(hit);
            }
        }
    }

    std::sort(hits.begin(), hits.end(), [](const RaycastHit& a, const RaycastHit& b) {
        return a.distance < b.distance;
        });

    return hits;
}

std::vector<Entity> Physics3D::OverlapSphere(const glm::vec3& center, float radius,
    const PhysicsQueryParams& params) {

    std::vector<Entity> overlapping;

    for (Entity entity : mEntities) {
        if (std::find(params.ignoreEntities.begin(), params.ignoreEntities.end(), entity) != params.ignoreEntities.end()) {
            continue;
        }

        OBB obb = BuildOBB(entity);
        glm::vec3 closestPoint = ClosestPointOnOBB(center, obb);
        float distSq = glm::length2(closestPoint - center);

        if (distSq <= radius * radius) {
            overlapping.push_back(entity);
        }
    }

    return overlapping;
}

std::vector<Entity> Physics3D::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
    const glm::quat& rotation, const PhysicsQueryParams& params) {

    std::vector<Entity> overlapping;

    // Create query OBB
    OBB queryOBB;
    queryOBB.center = center;
    queryOBB.halfExtents = halfExtents;
    glm::mat3 rotMat = glm::toMat3(rotation);
    queryOBB.axes[0] = rotMat[0];
    queryOBB.axes[1] = rotMat[1];
    queryOBB.axes[2] = rotMat[2];

    for (Entity entity : mEntities) {
        if (std::find(params.ignoreEntities.begin(), params.ignoreEntities.end(), entity) != params.ignoreEntities.end()) {
            continue;
        }

        OBB obb = BuildOBB(entity);

        // Simple OBB-OBB overlap test (SAT without full manifold)
        if (TestOBBOverlap(queryOBB, obb)) {
            overlapping.push_back(entity);
        }
    }

    return overlapping;
}

// Helper: Ray-OBB intersection test
bool Physics3D::RayOBBIntersection(const glm::vec3& origin, const glm::vec3& direction,
    const OBB& obb, float& tMin, float& tMax) {

    tMin = 0.0f;
    tMax = FLT_MAX;

    glm::vec3 p = obb.center - origin;

    for (int i = 0; i < 3; i++) {
        glm::vec3 axis = obb.axes[i];
        float e = glm::dot(axis, p);
        float f = glm::dot(axis, direction);

        if (glm::abs(f) > 0.0001f) {
            float t1 = (e + obb.halfExtents[i]) / f;
            float t2 = (e - obb.halfExtents[i]) / f;

            if (t1 > t2) std::swap(t1, t2);

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);

            if (tMin > tMax) return false;
        }
        else {
            if (-e - obb.halfExtents[i] > 0.0f || -e + obb.halfExtents[i] < 0.0f) {
                return false;
            }
        }
    }

    return true;
}

// Helper: Simple OBB overlap test
bool Physics3D::TestOBBOverlap(const OBB& a, const OBB& b) {
    glm::vec3 centerDist = b.center - a.center;

    // Test 6 face normals (3 from each OBB)
    for (int i = 0; i < 3; i++) {
        float rA = a.halfExtents[i];
        float rB = glm::abs(glm::dot(b.axes[0], a.axes[i]) * b.halfExtents.x) +
            glm::abs(glm::dot(b.axes[1], a.axes[i]) * b.halfExtents.y) +
            glm::abs(glm::dot(b.axes[2], a.axes[i]) * b.halfExtents.z);
        float dist = glm::abs(glm::dot(centerDist, a.axes[i]));
        if (dist > rA + rB) return false;
    }

    for (int i = 0; i < 3; i++) {
        float rB = b.halfExtents[i];
        float rA = glm::abs(glm::dot(a.axes[0], b.axes[i]) * a.halfExtents.x) +
            glm::abs(glm::dot(a.axes[1], b.axes[i]) * a.halfExtents.y) +
            glm::abs(glm::dot(a.axes[2], b.axes[i]) * a.halfExtents.z);
        float dist = glm::abs(glm::dot(centerDist, b.axes[i]));
        if (dist > rA + rB) return false;
    }

    // For a complete test, you'd also test 9 edge cross products
    // But this is sufficient for most cases

    return true;
}