#include "Animator.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMeshData.h"
#include "Engine/Animation/Animation.h"
#include "Engine/Animation/AssimpNodeData.h"
#include "Api/AssetManagerApi.h" 
#include <iostream>

void Animator::Update(float dt)
{
    for (auto const& entity : mEntities)
    {
        auto component = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));

        // Validation Checks
        if (!component || !component->MeshResource) continue;
        if (!component->MeshResource->cpuMesh) continue;

        // Cast to Skeletal Data
        auto skeletalMesh = std::static_pointer_cast<SkeletalMeshData>(component->MeshResource->cpuMesh);
        if (!skeletalMesh) continue;
        if (skeletalMesh->animations.empty()) continue;

        // --- CHANGE: Pure Evaluation Mode ---
        // We do NOT decide which animation to play. We blindly trust CurrentAnimationName.
        // The AnimationGraphSystem (or user code) is responsible for setting this.

        Animation* currentAnimation = nullptr;
        if (!component->CurrentAnimationName.empty())
        {
            auto it = skeletalMesh->animations.find(component->CurrentAnimationName);
            if (it != skeletalMesh->animations.end())
                currentAnimation = &it->second;
        }

        // Only calculate if we actually found the animation data
        if (currentAnimation)
        {
            // 1. Advance Time
            component->CurrentTime += currentAnimation->GetTicksPerSecond() * dt * component->AnimationSpeed;

            // Handle Looping (Graph might override this later, but safe default)
            float duration = currentAnimation->GetDuration();
            if (duration > 0.0f) {
                component->CurrentTime = fmod(component->CurrentTime, duration);
            }

            // 2. Calculate Matrices
            CalculateBoneTransform(&currentAnimation->GetRootNode(), glm::mat4(1.0f), component, currentAnimation, skeletalMesh);
        }
    }
}

// This function remains exactly the same as you had it
void Animator::CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform, SkeletalMeshComponent* component, Animation* animation, std::shared_ptr<SkeletalMeshData> skeletalMesh)
{
    std::string nodeName = node->name;
    glm::mat4 nodeTransform = node->transformation;

    Bone* Bone = animation->FindBone(nodeName);

    if (Bone)
    {
        Bone->Update(component->CurrentTime);
        nodeTransform = Bone->GetLocalTransform();
    }

    glm::mat4 globalTransformation = parentTransform * nodeTransform;

    auto& boneInfoMap = skeletalMesh->boneInfoMap;
    if (boneInfoMap.find(nodeName) != boneInfoMap.end())
    {
        int index = boneInfoMap[nodeName].id;
        glm::mat4 offset = boneInfoMap[nodeName].offset;

        if (component->FinalBoneMatrices.size() <= index) {
            component->FinalBoneMatrices.resize(index + 1, glm::mat4(1.0f));
        }

        component->FinalBoneMatrices[index] = globalTransformation * offset;
    }

    for (int i = 0; i < node->childrenCount; i++)
        CalculateBoneTransform(&node->children[i], globalTransformation, component, animation, skeletalMesh);
}

void Animator::Cleanup() {}

extern "C" REFLECTION_API System* CreateSystem()
{
    return new Animator();
}