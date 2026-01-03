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

    void Update(float dt) override;
    void Cleanup() override;

private:
    void CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform, SkeletalMeshComponent* component, Animation* animation, std::shared_ptr<SkeletalMeshData> skeletalMesh);
};

extern "C" REFLECTION_API System* CreateSystem();