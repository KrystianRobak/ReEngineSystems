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
        std::vector<std::string> ComponentsToRegister = { "SkeletalMeshComponent" };

    // Run after StateMachine so we pick up the latest requested animation
    REFVARIABLE()
        std::vector<std::string> SystemsToRunAfter = { "StateMachineSystem", "Physics3D" };

    REFVARIABLE()
        std::vector<std::string> WriteComponents = { "SkeletalMeshComponent" };

    void Update(float dt) override;
    void Cleanup() ;


    int GetBoneIndex(Animation* anim, const std::string& nodeName) {
        auto& cache = animCaches_[anim];

        // Build cache on first use
        if (cache.boneNameToIndex.empty()) {
            auto& boneProps = anim->getBoneProps();
            for (size_t i = 0; i < boneProps.size(); ++i) {
                cache.boneNameToIndex[boneProps[i].name] = i;
            }
        }

        auto it = cache.boneNameToIndex.find(nodeName);
        return (it != cache.boneNameToIndex.end()) ? it->second : -1;
    }

private:

    struct AnimationCache {
        std::unordered_map<std::string, int> boneNameToIndex;
    };

    std::unordered_map<Animation*, AnimationCache> animCaches_;

    // Standard playback
    void CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform,
        SkeletalMeshComponent* component, Animation* animation,
        std::shared_ptr<SkeletalMeshData> skeletalMesh);

    // Blending logic (PrevAnim -> NextAnim)
    void CalculateBoneTransition(const AssimpNodeData* node, glm::mat4 parentTransform,
        SkeletalMeshComponent* component,
        Animation* prevAnim, Animation* nextAnim,
        std::shared_ptr<SkeletalMeshData> skeletalMesh);

private:
};

extern "C" REFLECTION_API System* CreateSystem();