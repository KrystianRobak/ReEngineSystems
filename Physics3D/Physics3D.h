#pragma once

#include "ReflectionCoreExport.h"
#include "System\System.h"
#include "ReflectionMacros.h"
#include "ReflectionEngine.h"
#include "Collision/Octree.h"
#include <unordered_set>
#include <unordered_map>
#include <glm/glm.hpp>
#include "Logger.h"
#include "../Commander.h"

#include "Collision/Octree.h" 

struct Manifold {
    Entity entityA;
    Entity entityB;
    glm::vec3 normal;       // Points A -> B
    glm::vec3 contactPoint; // World space position of impact
    float depth;
    bool hasCollision;
};

REFSYSTEM()
class REFLECTION_API Physics3D : public System
{
public:
    // Register the new components so the engine knows about them
    REFVARIABLE()
        std::vector<std::string> ComponentsToRegister = { "Transform", "RigidBody"};

    void Update(float dt);
    void Cleanup();

    void OnInit() override;

    REFFUNCTION()
        void AddForceToEntity(Entity entity, glm::vec3 force); // Changed to vec3 for 3D

    REFVARIABLE()
        glm::vec3 gravity = glm::vec3(0.0f, -0.81f, 0.0f);

private:

    std::unordered_set<Entity> processedStaticMeshes;

    std::unique_ptr<Octree> octree;

    // --- Pipeline ---
    void SyncStaticColliders(); // Auto-fit props
    void SolveNarrowPhase(Entity a, Entity b);
    void ResolveCollision(const Manifold& m);

    // --- Solvers ---
    Manifold CheckBoxVsBox(Entity a, Entity b);
    Manifold CheckCapsuleVsMesh(Entity capsuleEnt, Entity meshEnt);

    // Broadphase Helpers
    std::vector<AABB*> activeColliders;
    std::unordered_map<AABB*, Entity> aabbToEntity;

    // Helper to detect penetration depth and normal between two AABBs
    bool GetCollisionManifold(const AABB& a, const AABB& b, Manifold& manifold);

    void CheckDynamicVsHeightfield(Entity dynamicEntity, Entity terrainEntity);

};

extern "C" REFLECTION_API System * CreateSystem();
