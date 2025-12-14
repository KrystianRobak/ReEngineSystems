#pragma once

#include <GL/glew.h>
#include "GLFW/glfw3.h"
#include <glm/glm.hpp>
#include "ReflectionCoreExport.h"
#include "System\System.h"
#include "ReflectionMacros.h"
#include "ReflectionEngine.h"
#include "Logger.h"

#include "../Commander.h"

#pragma once

#include "Collision/Octree.h" 

REFSYSTEM()
class REFLECTION_API Physics3D : public System
{
public:
    // Register the new components so the engine knows about them
    REFVARIABLE()
        std::vector<std::string> ComponentsToRegister = { "Transform", "RigidBody", "BoxCollider" };

    void Update(float dt);
    void Cleanup();

    REFFUNCTION()
        void AddForceToEntity(Entity entity, glm::vec3 force); // Changed to vec3 for 3D

private:
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);

    struct Manifold {
        Entity entityA;
        Entity entityB;
        glm::vec3 normal; // Points from A to B
        float penetration;
    };

    // Helper to detect penetration depth and normal between two AABBs
    bool GetCollisionManifold(const AABB& a, const AABB& b, Manifold& manifold);

    void CheckDynamicVsHeightfield(Entity dynamicEntity, Entity terrainEntity);

    // Helper to physically separate objects and update velocities
    void ResolveCollision(Manifold& m);
};

extern "C" REFLECTION_API System * CreateSystem();
