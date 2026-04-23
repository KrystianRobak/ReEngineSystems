#pragma once
#include "System/System.h"
#include "ReflectionMacros.h"
#include "ReflectionCoreExport.h"
#include <glm/glm.hpp>
#include "Animation/Animation.h"
#include <vector>
#include <memory>

// Forward Declarations
struct SkeletalMeshComponent;
struct SkeletalMeshData;
struct AssimpNodeData;

REFSYSTEM()
class REFLECTION_API Animator : public System
{
public:
    REFVARIABLE()
        std::vector<std::string> ComponentsToRegister = { "SkeletalMeshComponent, StateMachine" };

    REFVARIABLE()
        std::vector<std::string> SystemsToRunAfter = { "StateMachineSystem", "Physics3D" };

    REFVARIABLE()
        std::vector<std::string> WriteComponents = { "SkeletalMeshComponent" };

    void Update(float dt) override;
    void Cleanup() ;


    int GetBoneIndex(Animation* anim, const std::string& nodeName);

private:

    struct AnimationCache {
        std::unordered_map<std::string, int> boneNameToIndex;
    };

    std::unordered_map<Animation*, AnimationCache> animCaches_;

    void CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform,
        SkeletalMeshComponent* component, Animation* animation,
        std::shared_ptr<SkeletalMeshData> skeletalMesh);

    void CalculateBoneTransition(const AssimpNodeData* node, glm::mat4 parentTransform,
        SkeletalMeshComponent* component,
        Animation* prevAnim, Animation* nextAnim,
        std::shared_ptr<SkeletalMeshData> skeletalMesh);

private:
};

extern "C" REFLECTION_API System* CreateSystem();