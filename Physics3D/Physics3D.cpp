#include "Physics3D.h"
#include "Components/Transform.h"
#include "Components/RigidBody.h"   // Include your new component
#include "Components/BoxCollider.h" // Include your new component
#include "Components/HeightfieldCollider.h"
#include "Collision/Octree.h"       //
#include <algorithm>

// --- Internal Helper Functions (must match WorldGenSystem's generation logic) ---

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

// --- New Specialized Collision Function ---
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

void Physics3D::Update(float dt)
{
    std::vector<AABB*> activeColliders;
    // Map AABBs back to entities for resolution
    std::unordered_map<AABB*, Entity> aabbToEntityMap;

    // --- STEP 1: Integration (Apply Gravity & Move) ---
    for (Entity entity : mEntities)
    {
        Transform* transform = static_cast<Transform*>(engine_->GetComponent(entity, "Transform"));
        RigidBody* rb = static_cast<RigidBody*>(engine_->GetComponent(entity, "RigidBody"));

        // Skip physics for static objects (walls/floors)
        if (rb->isStatic) {
                BoxCollider* collider = static_cast<BoxCollider*>(engine_->GetComponent(entity, "BoxCollider"));
                collider->aabb->updatePosition(transform->position + collider->offset, transform->rotation, transform->scale * collider->size);

                activeColliders.push_back(collider->aabb.get());
                aabbToEntityMap[collider->aabb.get()] = entity;
            continue;
        }

        // Apply Gravity
        if (rb->useGravity) {
            rb->velocity += gravity * dt;
        }

        // Apply Acceleration
        rb->velocity += rb->acceleration * dt;
        rb->acceleration = glm::vec3(0.0f); // Reset frame acceleration

        // Apply Velocity to Position
        transform->position += rb->velocity * dt;
        engine_->MarkEntityDirty(entity, "Transform");

            BoxCollider* collider = static_cast<BoxCollider*>(engine_->GetComponent(entity, "BoxCollider"));
            // Update AABB using the new transform
            collider->aabb->updatePosition(transform->position + collider->offset, transform->rotation, transform->scale * collider->size);

            activeColliders.push_back(collider->aabb.get());
            aabbToEntityMap[collider->aabb.get()] = entity;
    }

    // --- STEP 2: Broadphase (Collision Detection) ---
    // Use the provided checkCollisions function which uses Octree
    // Note: You might need to make checkCollisions accessible or copy the logic here if it's static in Octree.cpp
    std::vector<std::pair<AABB*, AABB*>> potentialCollisions = checkCollisions(activeColliders);

    // --- STEP 3: Narrowphase & Resolution ---
    for (auto& pair : potentialCollisions) {
        AABB* a = pair.first;
        AABB* b = pair.second;

        Manifold m;
        m.entityA = aabbToEntityMap[a];
        m.entityB = aabbToEntityMap[b];

        if (GetCollisionManifold(*a, *b, m)) {
            ResolveCollision(m);
        }
    }
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
        m.penetration = x_overlap;
        m.normal = glm::vec3(n.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
    }
    else if (y_overlap < z_overlap) {
        m.penetration = y_overlap;
        m.normal = glm::vec3(0.0f, n.y > 0 ? 1.0f : -1.0f, 0.0f);
    }
    else {
        m.penetration = z_overlap;
        m.normal = glm::vec3(0.0f, 0.0f, n.z > 0 ? 1.0f : -1.0f);
    }

    return true;
}

void Physics3D::ResolveCollision(Manifold& m) {
    Transform* tA = static_cast<Transform*>(engine_->GetComponent(m.entityA, "Transform"));
    RigidBody* rbA = static_cast<RigidBody*>(engine_->GetComponent(m.entityA, "RigidBody"));

    Transform* tB = static_cast<Transform*>(engine_->GetComponent(m.entityB, "Transform"));
    RigidBody* rbB = static_cast<RigidBody*>(engine_->GetComponent(m.entityB, "RigidBody"));

    // 1. Position Correction (Prevent sinking)
    // Move dynamic objects apart immediately so they don't stay overlapping
    const float percent = 0.5f; // Penetration percentage to correct
    const float slop = 0.01f;   // Threshold
    float invMassA = rbA->isStatic ? 0.0f : 1.0f / rbA->mass;
    float invMassB = rbB->isStatic ? 0.0f : 1.0f / rbB->mass;
    float invMassSum = invMassA + invMassB;

    if (invMassSum == 0.0f) return; // Two static objects colliding

    glm::vec3 correction = (std::max(m.penetration - slop, 0.0f) / invMassSum) * percent * m.normal;

    if (!rbA->isStatic) tA->position -= correction * invMassA;
    if (!rbB->isStatic) tB->position += correction * invMassB;

    // 2. Impulse Resolution (Bounce/Slide)
    glm::vec3 relVel = rbB->velocity - rbA->velocity;
    float velAlongNormal = glm::dot(relVel, m.normal);

    // If objects are already moving apart, do nothing
    if (velAlongNormal > 0) return;

    // Calculate restitution (bounciness) - use the lower of the two
    float e = std::min(rbA->restitution, rbB->restitution);

    // Calculate impulse scalar
    float j = -(1 + e) * velAlongNormal;
    j /= invMassSum;

    // Apply impulse
    glm::vec3 impulse = j * m.normal;

    if (!rbA->isStatic) rbA->velocity -= impulse * invMassA;
    if (!rbB->isStatic) rbB->velocity += impulse * invMassB;

    // 3. Simple Friction (Dampen velocity tangent to normal)
    glm::vec3 tangent = relVel - (velAlongNormal * m.normal);
    if (glm::length(tangent) > 0.0001f) {
        tangent = glm::normalize(tangent);
        float mu = (rbA->friction + rbB->friction) * 0.5f;

        // Friction impulse
        float jt = -glm::dot(relVel, tangent);
        jt /= invMassSum;

        // Clamp friction (Coulomb's Law)
        if (std::abs(jt) < j * mu) {
            glm::vec3 frictionImpulse = jt * tangent;
            if (!rbA->isStatic) rbA->velocity -= frictionImpulse * invMassA;
            if (!rbB->isStatic) rbB->velocity += frictionImpulse * invMassB;
        }
    }
}

void Physics3D::AddForceToEntity(Entity entity, glm::vec3 force)
{
    if (engine_->HasComponent(entity, "RigidBody")) {
        RigidBody* rb = static_cast<RigidBody*>(engine_->GetComponent(entity, "RigidBody"));
        if (!rb->isStatic) {
            // F = ma -> a = F/m
            rb->acceleration += force / rb->mass;
        }
    }
}

void Physics3D::Cleanup() {}

extern "C" {
    REFLECTION_API System* CreateSystem()
    {
        return new Physics3D();
    }
}