#include "Animator.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMeshData.h"
#include "Animation/Animation.h"
#include "Animation/AssimpNodeData.h"
#include "Api/AssetManagerApi.h" 
#include <algorithm>
#include <iostream>

namespace AnimMath {
    float GetScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime) {
        float scaleFactor = 0.0f;
        float midWayLength = animationTime - lastTimeStamp;
        float framesDiff = nextTimeStamp - lastTimeStamp;
        scaleFactor = midWayLength / framesDiff;
        return scaleFactor;
    }

    glm::mat4 InterpolatePosition(float animationTime, KeyPosition from, KeyPosition to) {
        float scaleFactor = GetScaleFactor(from.timeStamp, to.timeStamp, animationTime);
        glm::vec3 finalPosition = glm::mix(from.position, to.position, scaleFactor);
        return glm::translate(glm::mat4(1.0f), finalPosition);
    }

    glm::mat4 InterpolateRotation(float animationTime, KeyRotation from, KeyRotation to) {
        float scaleFactor = GetScaleFactor(from.timeStamp, to.timeStamp, animationTime);
        glm::quat finalRotation = glm::slerp(from.orientation, to.orientation, scaleFactor);
        finalRotation = glm::normalize(finalRotation);
        return glm::toMat4(finalRotation);
    }

    glm::mat4 InterpolateScaling(float animationTime, KeyScale from, KeyScale to) {
        float scaleFactor = GetScaleFactor(from.timeStamp, to.timeStamp, animationTime);
        glm::vec3 finalScale = glm::mix(from.scale, to.scale, scaleFactor);
        return glm::scale(glm::mat4(1.0f), finalScale);
    }
}

void Animator::Update(float dt)
{
    auto assetManager = engine_->GetAssetManager();

    for (auto const& entity : mEntities)
    {
        auto component = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));
        if (!component || !component->MeshResource || !component->MeshResource->cpuMesh) continue;

        auto skeletalMesh = std::static_pointer_cast<SkeletalMeshData>(component->MeshResource->cpuMesh);

        // 1. Setup Buffers
        int boneCount = 100;
        if (component->FinalBoneMatrices.empty()) {  // Changed condition
            component->FinalBoneMatrices.resize(boneCount, glm::mat4(1.0f));
        }
        component->DebugBoneLines.clear();

        // 2. Detect State Machine Change
        // If the name requested by StateMachine is different from what we played last frame...
        if (component->CurrentAnimationName != component->LastAnimationName)
        {
            // Only blend if we actually had a previous animation
            if (!component->LastAnimationName.empty()) {
                component->IsBlending = true;
                component->BlendTimer = 0.0f;
                // Capture where we were last frame (before StateMachine might have reset CurrentTime)
                component->HaltTime = component->LastFrameTime;
            }
            // Update tracking
            component->LastAnimationName = component->CurrentAnimationName;
        }
        
        std::shared_ptr<Animation> currentAnim = assetManager->GetAnimation(component->CurrentAnimationName, skeletalMesh.get());
        
        // 3. Logic Branch: Blending vs Playing
        if (component->IsBlending)
        {

        }

        if (currentAnim)
        {
            // --- BLENDING LOGIC ---
            if (component->IsBlending)
            {
                
                std::shared_ptr<Animation> prevAnim = assetManager->GetAnimation(component->LastAnimationName, skeletalMesh.get());

                component->BlendTimer += component->CurrentAnimationName == component->LastAnimationName ? 0 : dt * currentAnim->getTicksPerSecond();
                // Wait, if I don't update LastAnimationName, Step 2 triggers every frame.
                // Logic fix:
                // Check change -> Set `TargetAnimationName`. `Last` remains `Old`. `IsBlending` = true.

                // Let's stick to the prompt's request "Make it work like this one".
                // The reference has `currentAnimation` and `nextAnimation`.
                // I will simulate this.

                // Advance Blending
                component->BlendTimer += currentAnim->getTicksPerSecond() * dt;

                if (component->BlendTimer > component->BlendDuration * currentAnim->getTicksPerSecond()) {
                    // Blend Finished
                    component->IsBlending = false;
                    component->CurrentTime = component->BlendTimer; // Sync time
                    component->LastAnimationName = component->CurrentAnimationName; // Now we are officially in the new state
                }
                else {
                    if (prevAnim && currentAnim) {
                        CalculateBoneTransition(
                            currentAnim->getRootNode(), glm::mat4(1.0f),
                            component, prevAnim.get(), currentAnim.get(),
                            skeletalMesh
                        );
                        goto EndLoop;
                    }
                    else {
                        component->IsBlending = false;
                    }
                }
            }
            
            component->CurrentTime += currentAnim->getTicksPerSecond() * dt * component->AnimationSpeed;
            float duration = currentAnim->getDuration();

            if (duration > 0.0f) {
                if (component->IsLooping) component->CurrentTime = fmod(component->CurrentTime, duration);
                else if (component->CurrentTime > duration) component->CurrentTime = duration;
            }

            CalculateBoneTransform(
                currentAnim->getRootNode(), glm::mat4(1.0f),
                component, currentAnim.get(),
                skeletalMesh
            );

            // Update tracking for next frame's "HaltTime"
            component->LastFrameTime = component->CurrentTime;

            // If we are not blending, we ensure Last matches Current so we detect changes later
            if (!component->IsBlending) {
                component->LastAnimationName = component->CurrentAnimationName;
            }
        }
        else {
            // Bind Pose
            std::fill(component->FinalBoneMatrices.begin(), component->FinalBoneMatrices.end(), glm::mat4(1.0f));
        }

    EndLoop:;
    }
}

// --- TRANSITION CALCULATOR ---
void Animator::CalculateBoneTransition(const AssimpNodeData* node, glm::mat4 parentTransform,
    SkeletalMeshComponent* component,
    Animation* prevAnim, Animation* nextAnim,
    std::shared_ptr<SkeletalMeshData> skeletalMesh)
{
    if (!node) return;
    std::string nodeName = node->name;
    glm::mat4 nodeTransform = node->transformation; // Default to bind pose

    Bone* prevBone = prevAnim->findBone(nodeName);
    Bone* nextBone = nextAnim->findBone(nodeName);

    if (prevBone && nextBone)
    {
        // 1. Sample Previous Animation at HALT time
        KeyPosition prevPos = prevBone->getPositions(component->HaltTime);
        KeyRotation prevRot = prevBone->getRotations(component->HaltTime);
        KeyScale prevScl = prevBone->getScalings(component->HaltTime);

        // 2. Sample Next Animation at BLEND time (usually starts at 0, but we advance it by BlendTimer)
        // The reference code interpolates from [Prev@Halt] to [Next@0].
        // But usually you want [Next@BlendTimer].
        // Reference code logic:
        // prevPos.timeStamp = 0.0f; nextPos.timeStamp = transitionTime; 
        // interpolate(currentTime, prev, next).

        float blendDurationTicks = component->BlendDuration * nextAnim->getTicksPerSecond();

        // Construct fake keys for interpolation
        KeyPosition targetPos = nextBone->getPositions(component->BlendTimer); // Moving target?
        // The reference code does a fixed transition to the start of the next anim.
        // Let's stick to the reference code's idea: transition to the start.
        KeyPosition startNextPos = nextBone->getPositions(0.0f);
        KeyRotation startNextRot = nextBone->getRotations(0.0f);
        KeyScale    startNextScl = nextBone->getScalings(0.0f);

        // Override timestamps for math helper
        prevPos.timeStamp = 0.0f;
        prevRot.timeStamp = 0.0f;
        prevScl.timeStamp = 0.0f;

        startNextPos.timeStamp = blendDurationTicks;
        startNextRot.timeStamp = blendDurationTicks;
        startNextScl.timeStamp = blendDurationTicks;

        // Perform Interpolation
        glm::mat4 p = AnimMath::InterpolatePosition(component->BlendTimer, prevPos, startNextPos);
        glm::mat4 r = AnimMath::InterpolateRotation(component->BlendTimer, prevRot, startNextRot);
        glm::mat4 s = AnimMath::InterpolateScaling(component->BlendTimer, prevScl, startNextScl);

        nodeTransform = p * r * s;
    }

    glm::mat4 globalTransformation = parentTransform * nodeTransform;

    // Map to Mesh
    // (In production, cache this map, don't search every frame)
    auto it = std::find_if(skeletalMesh->boneInfoMap.begin(), skeletalMesh->boneInfoMap.end(),
        [&](const BoneProps& props) { return props.name == nodeName; });

    if (it != skeletalMesh->boneInfoMap.end()) {
        int index = (int)std::distance(skeletalMesh->boneInfoMap.begin(), it);
        if (index < component->FinalBoneMatrices.size()) {
            component->FinalBoneMatrices[index] = globalTransformation * it->offset;
        }
    }

    // Recurse
    for (int i = 0; i < node->childrenCount; i++) {
        CalculateBoneTransition(&node->children[i], globalTransformation, component, prevAnim, nextAnim, skeletalMesh);
    }
}

std::string GetSuffixAfterDelimiter(const std::string& name, char delimiter)
{
    size_t pos = name.find(delimiter);
    if (pos == std::string::npos)
        return "";

    return name.substr(pos + 1);
}

// --- STANDARD CALCULATOR (Existing) ---
void Animator::CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform, SkeletalMeshComponent* component, Animation* animation, std::shared_ptr<SkeletalMeshData> skeletalMesh)
{
    if (!node) return;

    std::string nodeName = node->name;
    glm::mat4 nodeTransform = node->transformation;

    Bone* bone = animation->findBone(nodeName);
    if (bone) {
        bone->update(component->CurrentTime);
        nodeTransform = bone->getTransform();
    }

    glm::mat4 globalTransformation = parentTransform * nodeTransform;

    /*auto boneProps = animation->getBoneProps();

    for (unsigned int i = 0; i < boneProps.size(); i++)
    {
        if (boneProps[i].name == nodeName)
        {
            glm::mat4 offset = boneProps[i].offset;
            component->FinalBoneMatrices[i] = globalTransformation * offset;
            break;
        }
    }*/

    int boneIndex = GetBoneIndex(animation, nodeName);
    if (boneIndex >= 0) {
        auto& boneProps = animation->getBoneProps();
        component->FinalBoneMatrices[boneIndex] = globalTransformation * boneProps[boneIndex].offset;
    }

    for (int i = 0; i < node->childrenCount; i++) {
        CalculateBoneTransform(&node->children[i], globalTransformation, component, animation, skeletalMesh);
    }
}

void Animator::Cleanup() {}
int Animator::GetBoneIndex(Animation* anim, const std::string& nodeName)
{
    auto& cache = animCaches_[anim];

    if (cache.boneNameToIndex.empty()) {
        auto& boneProps = anim->getBoneProps();
        for (size_t i = 0; i < boneProps.size(); ++i) {
            cache.boneNameToIndex[boneProps[i].name] = i;
        }
    }

    auto it = cache.boneNameToIndex.find(nodeName);
    return (it != cache.boneNameToIndex.end()) ? it->second : -1;
}
extern "C" REFLECTION_API System* CreateSystem() { return new Animator(); }