#pragma once
#include "System/System.h"
#include "ReflectionMacros.h"
#include "ReflectionCoreExport.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

// Forward Declarations
struct SkeletalMeshComponent;
struct SkeletalMeshData;
class Animation;
struct AssimpNodeData;

REFSYSTEM()
class REFLECTION_API Animator : public System
{
public:
    REFVARIABLE()
        std::vector<std::string> ComponentsToRegister = { "SkeletalMeshComponent" };

    // --- DEPENDENCIES ---
    // Animator must run AFTER logic (state changes) and AFTER physics (if using root motion/ragdolls)
    REFVARIABLE()
        std::vector<std::string> SystemsToRunAfter = { "StateMachineSystem", "Physics3D" };

    // It writes to the mesh component, preparing matrices for the renderer
    REFVARIABLE()
        std::vector<std::string> WriteComponents = { "SkeletalMeshComponent" };

    void Update(float dt) override;
    void Cleanup();

private:
    void CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform, SkeletalMeshComponent* component, Animation* animation, std::shared_ptr<SkeletalMeshData> skeletalMesh);
};

extern "C" REFLECTION_API System* CreateSystem();