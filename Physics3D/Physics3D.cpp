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

extern "C" {
    REFLECTION_API System* CreateSystem() { return new Physics3D(); }
}

void Physics3D::OnInit() {
    octree = std::make_unique<Octree>(glm::vec3(0.0f), 2000.0f);
    staticsInitialized = false;
}

void Physics3D::Cleanup() { octree.reset(); }

void Physics3D::Update(float dt) {
    if (!engine_ || !octree) return;

    // 1. Auto-Fit Colliders (Essential for matching visuals)
    FitCollidersToMeshes();

    // 2. Setup
    aabbToEntityMap.clear();
    currentFrameDynamics.clear();
    if (!staticsInitialized) currentFrameStatics.clear();

    // 3. Gravity
    ApplyGravityAndForces(dt);

    // 4. Build Statics
    if (!staticsInitialized && !currentFrameStatics.empty()) {
        octree->BuildStatic(currentFrameStatics);
        staticsInitialized = true;
    }

    // 5. Broad & Narrow Phase
    FindCollisions();

    // 6. Solver (Iterative Impulse)
    // We solve 10 times to propagate forces through stacks of boxes
    for (int i = 0; i < SOLVER_ITERATIONS; ++i) {
        ResolveCollisions(dt);
    }

    // 7. Integration
    IntegratePositions(dt);
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

    // Standard Box Inertia Formula
    glm::vec3 I;
    I.x = (1.0f / 12.0f) * mass * (h * h + d * d);
    I.y = (1.0f / 12.0f) * mass * (w * w + d * d);
    I.z = (1.0f / 12.0f) * mass * (w * w + h * h);

    glm::mat3 I_invLocal(0.0f);
    I_invLocal[0][0] = 1.0f / I.x;
    I_invLocal[1][1] = 1.0f / I.y;
    I_invLocal[2][2] = 1.0f / I.z;

    glm::mat3 R = glm::toMat3(rotation);
    return R * I_invLocal * glm::transpose(R);
}

// --- The Core "Good Physics" Logic ---
bool Physics3D::CheckCollisionSAT(Entity entityA, Entity entityB, CollisionManifold& outManifold) {
    OBB a = BuildOBB(entityA);
    OBB b = BuildOBB(entityB);
    glm::vec3 centerDist = b.center - a.center;

    // 15 Axes Check (Standard SAT)
    glm::vec3 axes[15];
    int axisCount = 0;
    for (int i = 0; i < 3; i++) axes[axisCount++] = a.axes[i];
    for (int i = 0; i < 3; i++) axes[axisCount++] = b.axes[i];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        glm::vec3 cross = glm::cross(a.axes[i], b.axes[j]);
        if (glm::length(cross) > 0.001f) axes[axisCount++] = glm::normalize(cross);
    }

    float minPen = FLT_MAX;
    glm::vec3 bestAxis(0, 1, 0);

    for (int i = 0; i < axisCount; i++) {
        glm::vec3 axis = axes[i];
        float rA = glm::abs(glm::dot(a.axes[0], axis) * a.halfExtents.x) + glm::abs(glm::dot(a.axes[1], axis) * a.halfExtents.y) + glm::abs(glm::dot(a.axes[2], axis) * a.halfExtents.z);
        float rB = glm::abs(glm::dot(b.axes[0], axis) * b.halfExtents.x) + glm::abs(glm::dot(b.axes[1], axis) * b.halfExtents.y) + glm::abs(glm::dot(b.axes[2], axis) * b.halfExtents.z);
        float dist = glm::abs(glm::dot(centerDist, axis));
        float pen = (rA + rB) - dist;
        if (pen <= 0) return false; // Gap found
        if (pen < minPen) { minPen = pen; bestAxis = axis; }
    }

    if (glm::dot(bestAxis, centerDist) < 0) bestAxis = -bestAxis;

    if (glm::abs(bestAxis.y) > 0.99f) {
        bestAxis = glm::vec3(0, (bestAxis.y > 0 ? 1 : -1), 0);
    }

    outManifold.entityA = entityA;
    outManifold.entityB = entityB;
    outManifold.normal = bestAxis;

    // --- MANIFOLD GENERATION (This stops the spinning) ---
    // Instead of one point, we check all 8 corners of B to see if they are inside A
    // (This is a simplified "Box-Box" manifold strategy)

    // Transform B's corners into World Space
    glm::vec3 axisX = b.axes[0] * b.halfExtents.x;
    glm::vec3 axisY = b.axes[1] * b.halfExtents.y;
    glm::vec3 axisZ = b.axes[2] * b.halfExtents.z;
    glm::vec3 corners[8] = {
        b.center - axisX - axisY - axisZ, b.center + axisX - axisY - axisZ,
        b.center + axisX + axisY - axisZ, b.center - axisX + axisY - axisZ,
        b.center - axisX - axisY + axisZ, b.center + axisX - axisY + axisZ,
        b.center + axisX + axisY + axisZ, b.center - axisX + axisY + axisZ
    };

    // Check which corners of B are penetrating A along the best axis
    // This gives us a "face" of contacts instead of a single point
    for (const auto& corner : corners) {
        // Project corner onto the best axis
        // We calculate distance from A's center along the normal
        float distToA = glm::dot(corner - a.center, -bestAxis);

        // Calculate max extent of A along this normal to see if we are inside
        float extentA = glm::abs(glm::dot(a.axes[0], bestAxis) * a.halfExtents.x) +
            glm::abs(glm::dot(a.axes[1], bestAxis) * a.halfExtents.y) +
            glm::abs(glm::dot(a.axes[2], bestAxis) * a.halfExtents.z);

        float penetration = distToA + extentA; // simplified relative depth

        // If this corner is deep enough, it's a contact point
        // 0.02f is a small tolerance to catch flat-on-flat cases
        if (penetration > -0.05f) {
            outManifold.contacts.push_back({ corner, std::min(penetration, minPen) });
        }
    }

    // Fallback: If no corners found (e.g. edge-edge collision), use center average
    if (outManifold.contacts.empty()) {
        outManifold.contacts.push_back({ b.center - (bestAxis * b.halfExtents.y), minPen });
    }

    return true;
}

void Physics3D::ResolveCollisions(float dt) {
    for (const auto& m : manifolds) {
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

        // Solve for EACH contact point (Stabilizes the base)
        for (const auto& contact : m.contacts) {
            glm::vec3 rA = contact.position - txA->position;
            glm::vec3 rB = contact.position - txB->position;

            glm::vec3 velA = (rbA && !rbA->isStatic) ? rbA->velocity + glm::cross(rbA->angularVelocity, rA) : glm::vec3(0);
            glm::vec3 velB = (rbB && !rbB->isStatic) ? rbB->velocity + glm::cross(rbB->angularVelocity, rB) : glm::vec3(0);
            glm::vec3 relVel = velB - velA;
            float velAlongNormal = glm::dot(relVel, m.normal);

            if (velAlongNormal < 0) {
                // Rotational Impulse Denominator
                glm::vec3 raxn = glm::cross(rA, m.normal);
                glm::vec3 rbxn = glm::cross(rB, m.normal);
                float rotTerm = glm::dot(raxn, iA * raxn) + glm::dot(rbxn, iB * rbxn);

                float restitution = 0.2f;
                if (glm::abs(velAlongNormal) < 0.2f) { // 0.2 m/s threshold
                    restitution = 0.0f;
                }

                float j = -(1.0f + restitution) * velAlongNormal;
                j /= (totalInvMass + rotTerm);
                j /= (float)m.contacts.size();

                glm::vec3 impulse = j * m.normal;

                // Friction
                glm::vec3 tangent = relVel - (velAlongNormal * m.normal);
                if (glm::length(tangent) > 0.001f) {
                    tangent = glm::normalize(tangent);
                    float jt = -glm::dot(relVel, tangent) / (totalInvMass + rotTerm);
                    jt /= (float)m.contacts.size();
                    if (glm::abs(jt) < j * 0.4f) { // Friction Coefficient 0.4
                        impulse += jt * tangent;
                    }
                }

                if (rbA && !rbA->isStatic) {
                    rbA->velocity -= impulse * invMassA;
                    rbA->angularVelocity -= iA * glm::cross(rA, impulse);
                }
                if (rbB && !rbB->isStatic) {
                    rbB->velocity += impulse * invMassB;
                    rbB->angularVelocity += iB * glm::cross(rB, impulse);
                }
            }

            // Positional Correction (Baumgarte) - Prevents Sinking
            // We apply this per-contact too
            float depth = std::max(contact.penetration - 0.01f, 0.0f);
            if (depth > 0) {
                glm::vec3 correction = (depth / totalInvMass) * 0.2f * m.normal;
                correction /= (float)m.contacts.size();
                if (rbA && !rbA->isStatic) txA->position -= correction * invMassA;
                if (rbB && !rbB->isStatic) txB->position += correction * invMassB;
                SyncColliderTransform(m.entityA, txA);
                SyncColliderTransform(m.entityB, txB);
            }
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
        CollisionManifold manifold;
        if (CheckCollisionSAT(entityA, entityB, manifold)) manifolds.push_back(manifold);
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
            rb->velocity += (gravity + rb->acceleration) * dt;
            rb->acceleration = glm::vec3(0.0f);
            SyncColliderTransform(entity, tx);
            currentFrameDynamics.push_back(aabb);
            engine_->MarkEntityDirty(entity, "Transform");
        }
        else if (!staticsInitialized) {
            SyncColliderTransform(entity, tx);
            currentFrameStatics.push_back(aabb);
        }
    }
}

void Physics3D::IntegratePositions(float dt) {
    for (Entity entity : mEntities) {
        if (!engine_->HasComponent(entity, "RigidBody")) continue;
        RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
        if (rb->isStatic) continue;
        Transform* tx = (Transform*)engine_->GetComponentForWrite(entity, "Transform");

        // FIX 4: SLEEPING THRESHOLD
        // If the object has very little energy, force it to stop completely.
        // This stops the "ant-crawl" or "jitter" when objects should be still.
        if (glm::length(rb->velocity) < 0.1f && glm::length(rb->angularVelocity) < 0.1f) {
            rb->velocity = glm::vec3(0.0f);
            rb->angularVelocity = glm::vec3(0.0f);
        }
        else {
            tx->position += rb->velocity * dt;
            rb->velocity *= 0.99f; // Linear Damping
            rb->angularVelocity *= 0.95f; // Angular Damping

            if (glm::length(rb->angularVelocity) > 0.001f) {
                glm::quat step = glm::angleAxis(glm::length(rb->angularVelocity) * dt, glm::normalize(rb->angularVelocity));
                tx->rotation = step * tx->rotation;
                tx->rotation = glm::normalize(tx->rotation);
            }
            SyncColliderTransform(entity, tx);
        }
    }
}
// ... (Keep existing GetEntityAABB, SyncColliderTransform, ResolveHeightfieldCollision) ...

AABB* Physics3D::GetEntityAABB(Entity entity) {
    if (engine_->HasComponent(entity, "BoxCollider")) return ((BoxCollider*)engine_->GetComponent(entity, "BoxCollider"))->aabb.get();
    if (engine_->HasComponent(entity, "CapsuleCollider")) return ((CapsuleCollider*)engine_->GetComponent(entity, "CapsuleCollider"))->aabb.get();
    if (engine_->HasComponent(entity, "MeshCollider")) return ((MeshCollider*)engine_->GetComponent(entity, "MeshCollider"))->aabb.get();
    if (engine_->HasComponent(entity, "HeightfieldCollider")) return ((HeightfieldCollider*)engine_->GetComponent(entity, "HeightfieldCollider"))->aabb.get();
    return nullptr;
}

void Physics3D::SyncColliderTransform(Entity entity, Transform* transform) {
    if (engine_->HasComponent(entity, "BoxCollider")) ((BoxCollider*)engine_->GetComponent(entity, "BoxCollider"))->Update(transform->position, transform->rotation, transform->scale);
    else if (engine_->HasComponent(entity, "CapsuleCollider")) ((CapsuleCollider*)engine_->GetComponent(entity, "CapsuleCollider"))->Update(transform->position, transform->rotation, transform->scale);
    else if (engine_->HasComponent(entity, "MeshCollider")) ((MeshCollider*)engine_->GetComponent(entity, "MeshCollider"))->Update(transform->position, transform->rotation, transform->scale);
    else if (engine_->HasComponent(entity, "HeightfieldCollider")) ((HeightfieldCollider*)engine_->GetComponent(entity, "HeightfieldCollider"))->Update(transform->position);
}

void Physics3D::ResolveHeightfieldCollision(Entity dynamicEntity, Entity terrainEntity) {
    auto* rb = (RigidBody*)engine_->GetComponent(dynamicEntity, "RigidBody");
    auto* tx = (Transform*)engine_->GetComponentForWrite(dynamicEntity, "Transform");
    auto* hf = (HeightfieldCollider*)engine_->GetComponent(terrainEntity, "HeightfieldCollider");
    auto* terrainTx = (Transform*)engine_->GetComponent(terrainEntity, "Transform");

    if (!rb || !tx || !hf) return;

    float relX = tx->position.x - terrainTx->position.x;
    float relZ = tx->position.z - terrainTx->position.z;

    if (relX < -hf->halfWidth || relX > hf->halfWidth || relZ < -hf->halfDepth || relZ > hf->halfDepth) return;

    float terrainHeight = hf->GetHeightAt(relX, relZ) + terrainTx->position.y;
    float objectBottom = tx->position.y - (0.5f * tx->scale.y);

    if (objectBottom < terrainHeight) {
        float penetration = terrainHeight - objectBottom;
        tx->position.y += penetration;

        if (rb->velocity.y < 0) rb->velocity.y = 0;

        // Simple friction
        rb->velocity.x *= 0.95f;
        rb->velocity.z *= 0.95f;

        SyncColliderTransform(dynamicEntity, tx);
    }
}

void Physics3D::AddForceToEntity(Entity entity, glm::vec3 force) {
    if (engine_->HasComponent(entity, "RigidBody")) {
        RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
        if (rb && !rb->isStatic) rb->acceleration += force / rb->mass;
    }
}