#pragma once
#include "System/System.h"
#include "ReflectionMacros.h"
#include "ReflectionCoreExport.h"
#include "PhysicsWorld.h"
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

// --- PHYSX INCLUDES ---
#include <psyhx/PxPhysicsAPI.h>

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
using namespace physx;

REFSYSTEM()
class REFLECTION_API Physics3D : public System, public PhysicsWorld {
public:
    REFVARIABLE() std::vector<std::string> ComponentsToRegister = {
        "Transform", "RigidBody", "BoxCollider",
    };

    REFVARIABLE() std::vector<std::string> SystemsToRunAfter = { "StateMachineSystem" };
    REFVARIABLE() std::vector<std::string> WriteComponents = { "Transform", "RigidBody" };

    void OnInit() override;
    void Update(float dt) override;
    void Cleanup();

    // ===========================
    // PHYSICS QUERY API (PhysX Implementations)
    // ===========================

    bool RaycastSingle(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
        RaycastHit& outHit, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    std::vector<RaycastHit> RaycastAll(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    bool SphereCastSingle(const glm::vec3& origin, float radius, const glm::vec3& direction,
        float maxDistance, RaycastHit& outHit, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    std::vector<RaycastHit> SphereCastAll(const glm::vec3& origin, float radius,
        const glm::vec3& direction, float maxDistance, const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    std::vector<Entity> OverlapSphere(const glm::vec3& center, float radius,
        const PhysicsQueryParams& params = PhysicsQueryParams()) override;

    std::vector<Entity> OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
        const glm::quat& rotation = glm::quat(1, 0, 0, 0),
        const PhysicsQueryParams& params = PhysicsQueryParams()) override;

private:
    // --- PHYSX CORE OBJECTS ---
    PxFoundation* mFoundation = nullptr;
    PxPhysics* mPhysics = nullptr;
    PxDefaultCpuDispatcher* mDispatcher = nullptr;
    PxScene* mScene = nullptr;
    PxPvd* mPvd = nullptr; // Visual Debugger
    PxMaterial* mDefaultMaterial = nullptr;

    // Map to keep track of which Entity owns which PhysX Actor
    std::unordered_map<Entity, PxRigidActor*> mEntityActorMap;

    // --- HELPERS ---
    void InitPhysX();
    void CreateActorForEntity(Entity entity);
    void DestroyActorForEntity(Entity entity);
    void SyncECSToPhysX();
    // Original helper to auto-size boxes to meshes
    void FitCollidersToMeshes();

    // Converters
    PxVec3 ToPxVec3(const glm::vec3& v) { return PxVec3(v.x, v.y, v.z); }
    PxQuat ToPxQuat(const glm::quat& q) { return PxQuat(q.x, q.y, q.z, q.w); }
    glm::vec3 ToGlmVec3(const PxVec3& v) { return glm::vec3(v.x, v.y, v.z); }
    glm::quat ToGlmQuat(const PxQuat& q) { return glm::quat(q.w, q.x, q.y, q.z); }
};

extern "C" REFLECTION_API System* CreateSystem();