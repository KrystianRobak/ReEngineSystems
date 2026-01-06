#pragma once
#include "System/System.h"
#include "ReflectionMacros.h"
#include "ReflectionCoreExport.h"
#include "Octree.h"
#include <vector>
#include <glm/glm.hpp>

// Helper for OBB logic
struct OBB {
    glm::vec3 center;
    glm::vec3 axes[3];
    glm::vec3 halfExtents;
};

REFSYSTEM()
class REFLECTION_API Physics3D : public System {
public:
    REFVARIABLE() std::vector<std::string> ComponentsToRegister = {
        "Transform", "RigidBody", "BoxCollider", "StaticMesh"
    };

    // --- DEPENDENCIES ---
    // Physics must process the forces applied by the StateMachine
    REFVARIABLE()
        std::vector<std::string> SystemsToRunAfter = { "StateMachineSystem" };

    REFVARIABLE()
        std::vector<std::string> WriteComponents = { "Transform", "RigidBody" };

    void Update(float dt) override;
    void OnInit() override;
    void Cleanup();

private:
    std::unique_ptr<Octree> octree;
    bool staticsInitialized = false;
    std::unordered_map<AABB*, Entity> aabbToEntityMap;
    std::vector<AABB*> currentFrameDynamics;
    std::vector<AABB*> currentFrameStatics;
    std::vector<std::pair<AABB*, AABB*>> potentialCollisions;

    // A contact point in the manifold
    struct ContactPoint {
        glm::vec3 position;
        float penetration;
    };

    struct CollisionManifold {
        Entity entityA;
        Entity entityB;
        glm::vec3 normal;       // Points A -> B
        std::vector<ContactPoint> contacts; // Stable base needs up to 4 points
    };

    std::vector<CollisionManifold> manifolds;
    const glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    const int SOLVER_ITERATIONS = 10; // High iterations = hard objects

    // Helpers
    AABB* GetEntityAABB(Entity entity);
    void SyncColliderTransform(Entity entity, class Transform* transform);
    void ResolveHeightfieldCollision(Entity dynamicEntity, Entity terrainEntity);
    void AddForceToEntity(Entity entity, glm::vec3 force);
    void FitCollidersToMeshes();
    OBB BuildOBB(Entity entity);
    glm::mat3 ComputeInverseInertiaTensor(float mass, const glm::vec3& halfExtents, const glm::quat& rotation);

    // Phases
    void ApplyGravityAndForces(float dt);
    void FindCollisions();
    void ResolveCollisions(float dt);
    void IntegratePositions(float dt);

    bool CheckCollisionSAT(Entity a, Entity b, CollisionManifold& outManifold);
};
extern "C" REFLECTION_API System* CreateSystem();