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
        if (component->FinalBoneMatrices.empty()) {
            component->FinalBoneMatrices.resize(boneCount, glm::mat4(1.0f));
        }
        component->DebugBoneLines.clear();

        // 2. Detect Animation Change
        // FIX: We only trigger a new blend if we are NOT already blending.
        // Crucially, we do NOT overwrite LastAnimationName here — it must stay as
        // the previous animation's name so CalculateBoneTransition can fetch it.
        // LastAnimationName is only updated to CurrentAnimationName once the blend
        // fully completes (or immediately if there was no previous animation to blend from).
        if (component->CurrentAnimationName != component->LastAnimationName && !component->IsBlending)
        {
            if (!component->LastAnimationName.empty())
            {
                // Start a blend from LastAnimationName -> CurrentAnimationName.
                // LastAnimationName intentionally NOT updated yet — it is used below
                // as the "previous" animation source throughout the blend duration.
                component->IsBlending = true;
                component->BlendTimer = 0.0f;
                component->HaltTime = component->LastFrameTime;
            }
            else
            {
                // No previous animation — jump cut is fine, sync names immediately.
                component->LastAnimationName = component->CurrentAnimationName;
            }
        }

        std::shared_ptr<Animation> currentAnim = assetManager->GetAnimation(component->CurrentAnimationName, skeletalMesh.get());

        if (currentAnim)
        {
            // --- BLENDING LOGIC ---
            if (component->IsBlending)
            {
                std::shared_ptr<Animation> prevAnim = assetManager->GetAnimation(component->LastAnimationName, skeletalMesh.get());

                // FIX: BlendTimer incremented exactly ONCE per frame.
                // The original code incremented it twice inside the IsBlending block
                // (once conditionally, once unconditionally), making blends finish in
                // half the time and producing a visible jump cut.
                component->BlendTimer += currentAnim->getTicksPerSecond() * dt;

                if (component->BlendTimer >= component->BlendDuration * currentAnim->getTicksPerSecond())
                {
                    // Blend finished — the new animation fully takes over.
                    component->IsBlending = false;
                    component->CurrentTime = component->BlendTimer;
                    // NOW it is safe to sync LastAnimationName so next change is detected.
                    component->LastAnimationName = component->CurrentAnimationName;
                }
                else if (prevAnim)
                {
                    CalculateBoneTransition(
                        currentAnim->getRootNode(), glm::mat4(1.0f),
                        component, prevAnim.get(), currentAnim.get(),
                        skeletalMesh
                    );
                    component->LastFrameTime = component->HaltTime;
                    goto EndLoop;
                }
                else
                {
                    // prevAnim failed to load — abort blend gracefully.
                    component->IsBlending = false;
                    component->LastAnimationName = component->CurrentAnimationName;
                }
            }

            // --- NORMAL PLAYBACK ---
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

            component->LastFrameTime = component->CurrentTime;
        }
        else
        {
            // No animation loaded — bind pose.
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
    glm::mat4 nodeTransform = node->transformation;

    Bone* prevBone = prevAnim->findBone(nodeName);
    Bone* nextBone = nextAnim->findBone(nodeName);

    if (prevBone && nextBone)
    {
        // Sample previous animation at the halt time (the frame we froze it on).
        KeyPosition prevPos = prevBone->getPositions(component->HaltTime);
        KeyRotation prevRot = prevBone->getRotations(component->HaltTime);
        KeyScale    prevScl = prevBone->getScalings(component->HaltTime);

        // Sample next animation at time 0 (start of new animation).
        KeyPosition startNextPos = nextBone->getPositions(0.0f);
        KeyRotation startNextRot = nextBone->getRotations(0.0f);
        KeyScale    startNextScl = nextBone->getScalings(0.0f);

        float blendDurationTicks = component->BlendDuration * nextAnim->getTicksPerSecond();

        // Remap timestamps so the interpolation helpers treat BlendTimer as
        // the interpolant in [0, blendDurationTicks].
        prevPos.timeStamp = 0.0f;  prevRot.timeStamp = 0.0f;  prevScl.timeStamp = 0.0f;
        startNextPos.timeStamp = blendDurationTicks;
        startNextRot.timeStamp = blendDurationTicks;
        startNextScl.timeStamp = blendDurationTicks;

        glm::mat4 p = AnimMath::InterpolatePosition(component->BlendTimer, prevPos, startNextPos);
        glm::mat4 r = AnimMath::InterpolateRotation(component->BlendTimer, prevRot, startNextRot);
        glm::mat4 s = AnimMath::InterpolateScaling(component->BlendTimer, prevScl, startNextScl);

        nodeTransform = p * r * s;
    }

    glm::mat4 globalTransformation = parentTransform * nodeTransform;

    auto it = std::find_if(skeletalMesh->boneInfoMap.begin(), skeletalMesh->boneInfoMap.end(),
        [&](const BoneProps& props) { return props.name == nodeName; });

    if (it != skeletalMesh->boneInfoMap.end()) {
        int index = (int)std::distance(skeletalMesh->boneInfoMap.begin(), it);
        // FIX: bounds check before write — prevents undefined behaviour / GPU corruption
        // when a bone index exceeds the FinalBoneMatrices buffer size.
        if (index < (int)component->FinalBoneMatrices.size()) {
            component->FinalBoneMatrices[index] = globalTransformation * it->offset;
        }
    }

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

// --- STANDARD CALCULATOR ---
void Animator::CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform,
    SkeletalMeshComponent* component, Animation* animation,
    std::shared_ptr<SkeletalMeshData> skeletalMesh)
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

    int boneIndex = GetBoneIndex(animation, nodeName);
    if (boneIndex >= 0)
    {
        auto& boneProps = animation->getBoneProps();
        // FIX: Double bounds check — boneIndex must be valid in BOTH the boneProps array
        // AND FinalBoneMatrices. Without this, an out-of-range write corrupts GPU buffer
        // data, causing the mesh to vanish or explode at animation end.
        if (boneIndex < (int)boneProps.size() &&
            boneIndex < (int)component->FinalBoneMatrices.size())
        {
            component->FinalBoneMatrices[boneIndex] = globalTransformation * boneProps[boneIndex].offset;
        }
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