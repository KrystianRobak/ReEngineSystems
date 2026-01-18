#pragma once
#include "System/System.h"
#include "ReflectionMacros.h"
#include "ReflectionCoreExport.h"
#include "Octree.h"
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "PhysicsWorld.h"

// Helper for OBB logic
struct OBB {
    glm::vec3 center;
    glm::vec3 axes[3];
    glm::vec3 halfExtents;
};

// A contact point in the manifold
struct ContactPoint {
    glm::vec3 position;
    float penetration;
};

struct CollisionManifold {
    Entity entityA;
    Entity entityB;
    glm::vec3 normal;       // Points A -> B
    std::vector<ContactPoint> contacts; // Up to 4 points for stability
};

// CONTACT CACHING: Stores contact data between frames for warm starting
struct PersistentContact {
    Entity entityA;
    Entity entityB;
    glm::vec3 normal;
    float accumulatedNormalImpulse[4] = { 0, 0, 0, 0 };  // One per contact point
    float accumulatedTangentImpulse[4] = { 0, 0, 0, 0 }; // For friction
    int framesSinceUpdate = 0;
};

// ISLAND-BASED SLEEPING: Groups of connected objects that sleep together
struct SimulationIsland {
    std::vector<Entity> entities;
    std::vector<CollisionManifold> manifolds;
    bool isSleeping = false;
    float sleepTimer = 0.0f;
};

// PHYSICS QUERY SYSTEM: For raycasts, sphere casts, overlaps
//struct RaycastHit {
//    Entity entity = 0;
//    glm::vec3 position = glm::vec3(0.0f);
//    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
//    float distance = 0.0f;
//};
//
//struct PhysicsQueryParams {
//    std::vector<std::string> channels;      // Filter by channel/layer (requires tag system)
//    std::vector<Entity> ignoreEntities;     // Entities to skip
//    bool includeStatic = true;              // Include static objects
//    bool includeDynamic = true;             // Include dynamic objects
//};

REFSYSTEM()
class REFLECTION_API Physics3D : public System, public PhysicsWorld {
public:
    REFVARIABLE() std::vector<std::string> ComponentsToRegister = {
        "Transform", "RigidBody", "BoxCollider",
    };

    REFVARIABLE()
        std::vector<std::string> SystemsToRunAfter = { "StateMachineSystem" };

    REFVARIABLE()
        std::vector<std::string> WriteComponents = { "Transform", "RigidBody" };

    void Update(float dt) override;
    void OnInit() override;
    void Cleanup();

    // ===========================
    // PHYSICS QUERY API
    // ===========================

    // Raycast: Returns first hit along ray
    bool RaycastSingle(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
        RaycastHit& outHit, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    // Raycast: Returns all hits along ray (sorted by distance)
    std::vector<RaycastHit> RaycastAll(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    // Sphere Cast: Sweeps a sphere along a direction
    bool SphereCastSingle(const glm::vec3& origin, float radius, const glm::vec3& direction,
        float maxDistance, RaycastHit& outHit, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    std::vector<RaycastHit> SphereCastAll(const glm::vec3& origin, float radius,
        const glm::vec3& direction, float maxDistance, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    // Overlap Queries: Returns all entities overlapping a shape
    std::vector<Entity> OverlapSphere(const glm::vec3& center, float radius,
        const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    std::vector<Entity> OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
        const glm::quat& rotation = glm::quat(1, 0, 0, 0),
        const PhysicsQueryParams& params = PhysicsQueryParams()) override;

private:
    // Core structures
    std::unique_ptr<Octree> octree;
    bool staticsInitialized = false;
    std::unordered_map<AABB*, Entity> aabbToEntityMap;
    std::vector<AABB*> currentFrameDynamics;
    std::vector<AABB*> currentFrameStatics;
    std::vector<std::pair<AABB*, AABB*>> potentialCollisions;
    std::vector<CollisionManifold> manifolds;

    // Contact caching for warm starting
    std::unordered_map<uint64_t, PersistentContact> contactCache;

    // Island simulation
    std::vector<SimulationIsland> islands;

    const glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    const int SOLVER_ITERATIONS = 10;

    // Helpers
    AABB* GetEntityAABB(Entity entity);
    void SyncColliderTransform(Entity entity, class Transform* transform);
    void ResolveHeightfieldCollision(Entity dynamicEntity, Entity terrainEntity);
    void AddForceToEntity(Entity entity, glm::vec3 force);
    void FitCollidersToMeshes();
    OBB BuildOBB(Entity entity);
    glm::mat3 ComputeInverseInertiaTensor(float mass, const glm::vec3& halfExtents, const glm::quat& rotation);

    // Contact caching
    uint64_t MakePairKey(Entity a, Entity b);
    void CleanContactCache();

    // Island management
    void BuildIslands();
    void UpdateIslandSleeping(float dt);
    void WakeIsland(Entity entity);

    // Physics phases
    void ApplyGravityAndForces(float dt);
    void FindCollisions();
    void ResolveIslandCollisions(SimulationIsland& island, float dt);
    void IntegratePositions(float dt);
    bool CheckCollisionSAT(Entity a, Entity b, CollisionManifold& outManifold);

    // Query helpers
    bool RayOBBIntersection(const glm::vec3& origin, const glm::vec3& direction,
        const OBB& obb, float& tMin, float& tMax);
    bool TestOBBOverlap(const OBB& a, const OBB& b);
};

extern "C" REFLECTION_API System* CreateSystem();