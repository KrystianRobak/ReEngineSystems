#include "Animator.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMeshData.h"
#include "Animation/Animation.h"
#include "Animation/AssimpNodeData.h"
#include "Api/AssetManagerApi.h" 
#include <iostream>

// --- DIAGNOSTIC HELPER ---
void PrintMat4(const std::string& name, const glm::mat4& mat) {
    std::cout << name << ":\n";
    for (int i = 0; i < 4; i++) {
        std::cout << "  [";
        for (int j = 0; j < 4; j++) {
            std::cout << mat[j][i] << (j < 3 ? ", " : "");
        }
        std::cout << "]\n";
    }
}

void Animator::Update(float dt)
{
    static bool debugPrinted = false; // Only print once for debugging

    for (auto const& entity : mEntities)
    {
        auto component = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));

        if (!component || !component->MeshResource) continue;
        if (!component->MeshResource->cpuMesh) continue;

        auto skeletalMesh = std::static_pointer_cast<SkeletalMeshData>(component->MeshResource->cpuMesh);
        if (!skeletalMesh) continue;

        std::shared_ptr<Animation> currentAnimation = nullptr;

        if (!component->CurrentAnimationName.empty())
        {
            currentAnimation = engine_->GetAssetManager()->GetAnimation(component->CurrentAnimationName);
        }

        if (currentAnimation)
        {
            int boneCount = skeletalMesh->boneCount;

            // **CRITICAL FIX: Resize to actual bone count, not just check**
            component->FinalBoneMatrices.clear();
            component->FinalBoneMatrices.resize(boneCount, glm::mat4(1.0f));

            // Advance Time
            component->CurrentTime += currentAnimation->GetTicksPerSecond() * dt * component->AnimationSpeed;

            float duration = currentAnimation->GetDuration();
            if (duration > 0.0f) {
                if (component->IsLooping)
                    component->CurrentTime = fmod(component->CurrentTime, duration);
                else if (component->CurrentTime > duration)
                    component->CurrentTime = duration;
            }

            // **CRITICAL: Get the root node properly**
            const AssimpNodeData& rootNode = currentAnimation->GetRootNode();

            // --- DIAGNOSTIC OUTPUT (First Frame Only) ---
            if (!debugPrinted) {
                std::cout << "\n=== ANIMATION DEBUG INFO ===\n";
                std::cout << "Animation: " << component->CurrentAnimationName << "\n";
                std::cout << "Duration: " << duration << " ticks\n";
                std::cout << "TPS: " << currentAnimation->GetTicksPerSecond() << "\n";
                std::cout << "Current Time: " << component->CurrentTime << "\n";
                std::cout << "Bone Count: " << boneCount << "\n";
                std::cout << "Root Node: " << rootNode.name << "\n";
                std::cout << "Root Transform:\n";
                PrintMat4("  Root", rootNode.transformation);

                // Print all bones in the animation
                std::cout << "\nBones in boneInfoMap:\n";
                for (const auto& [name, info] : skeletalMesh->boneInfoMap) {
                    std::cout << "  - " << name << " (ID: " << info.id << ")\n";
                }

                debugPrinted = true;
            }

            // **KEY FIX: Pass IDENTITY as root, not the root transformation**
            // The root transformation should be applied by the hierarchy traversal
            CalculateBoneTransform(&rootNode, glm::mat4(1.0f), component, currentAnimation.get(), skeletalMesh);

            // --- VERIFY OUTPUT ---
            if (!debugPrinted) {
                std::cout << "\nFinal Bone Matrices (first 3):\n";
                for (int i = 0; i < std::min(3, (int)component->FinalBoneMatrices.size()); i++) {
                    std::cout << "Bone " << i << ":\n";
                    PrintMat4("  Matrix", component->FinalBoneMatrices[i]);
                }
                std::cout << "===========================\n\n";
            }
        }
        else
        {
            // Bind pose fallback
            int boneCount = skeletalMesh->boneCount;
            component->FinalBoneMatrices.clear();
            component->FinalBoneMatrices.resize(boneCount, glm::mat4(1.0f));
        }
    }
}

void Animator::CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform,
    SkeletalMeshComponent* component, Animation* animation,
    std::shared_ptr<SkeletalMeshData> skeletalMesh)
{
    if (!node) return;

    std::string nodeName = node->name;
    glm::mat4 nodeTransform = node->transformation;

    // **CRITICAL FIX: Check if this node has animation data**
    Bone* bone = animation->FindBone(nodeName);

    if (bone)
    {
        // This bone is animated - use the animation transform
        bone->Update(component->CurrentTime);
        nodeTransform = bone->GetLocalTransform();
    }
    // else: use the static transformation from the node hierarchy

    // **KEY CALCULATION: Global = Parent * Local**
    glm::mat4 globalTransformation = parentTransform * nodeTransform;

    // **CRITICAL: Only write to FinalBoneMatrices if this is actually a bone**
    auto& boneInfoMap = skeletalMesh->boneInfoMap;
    auto boneIt = boneInfoMap.find(nodeName);

    if (boneIt != boneInfoMap.end())
    {
        int boneID = boneIt->second.id;
        glm::mat4 offsetMatrix = boneIt->second.offset;

        // Safety check
        if (boneID >= 0 && boneID < component->FinalBoneMatrices.size())
        {
            // **THE MAGIC FORMULA:**
            // FinalMatrix = GlobalTransform * OffsetMatrix
            // 
            // OffsetMatrix = Inverse Bind Pose (transforms from mesh space to bone space)
            // GlobalTransform = Current animated bone position in world/model space
            // 
            // This formula says: "Take the vertex, transform it to bone-local space (offset),
            // then transform it to the current animated position (global)"
            component->FinalBoneMatrices[boneID] = globalTransformation * offsetMatrix;
        }
        else
        {
            std::cerr << "[Animator] ERROR: Bone ID " << boneID << " (" << nodeName
                << ") out of range [0, " << component->FinalBoneMatrices.size() << ")\n";
        }
    }

    // **CRITICAL: Recurse through ALL children, not just named bones**
    for (int i = 0; i < node->childrenCount && i < node->children.size(); i++)
    {
        CalculateBoneTransform(&node->children[i], globalTransformation, component, animation, skeletalMesh);
    }
}

void Animator::Cleanup() {}

extern "C" REFLECTION_API System* CreateSystem()
{
    return new Animator();
}