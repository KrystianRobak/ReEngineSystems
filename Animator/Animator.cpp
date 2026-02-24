#include "Animator.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMeshData.h"
#include "Animation/Animation.h"
#include "Animation/AssimpNodeData.h"
#include "Api/AssetManagerApi.h" 
#include <algorithm>
#include <iostream>
#include <cmath>

static inline float Clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline bool IsFiniteMat4(const glm::mat4& m)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r])) return false;
    return true;
}

void Animator::Update(float dt)
{
    if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;

    auto assetManager = engine_->GetAssetManager();

    for (auto const& entity : mEntities)
    {
        auto component = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));
        if (!component || !component->MeshResource || !component->MeshResource->cpuMesh) continue;

        auto skeletalMesh = std::static_pointer_cast<SkeletalMeshData>(component->MeshResource->cpuMesh);

        
        int boneCount = 100;
        if (component->FinalBoneMatrices.empty()) {
            component->FinalBoneMatrices.resize(boneCount, glm::mat4(1.0f));
        }
        std::fill(component->FinalBoneMatrices.begin(), component->FinalBoneMatrices.end(), glm::mat4(1.0f));
        component->DebugBoneLines.clear();

        
        
        
        
        
        
        if (component->CurrentAnimationName != component->LastAnimationName && !component->IsBlending)
        {
            if (!component->LastAnimationName.empty())
            {
                
                
                
                if (component->BlendDuration <= 0.0f) {
                    component->IsBlending = false;
                    component->LastAnimationName = component->CurrentAnimationName;
                    component->CurrentTime = 0.0f;
                }
                else {
                    component->IsBlending = true;
                    component->BlendTimer = 0.0f;
                    component->HaltTime = component->LastFrameTime;
                }
            }
            else
            {
                
                component->LastAnimationName = component->CurrentAnimationName;
            }
        }

        std::shared_ptr<Animation> currentAnim = assetManager->GetAnimation(component->CurrentAnimationName, skeletalMesh.get());

        if (currentAnim)
        {
            
            if (component->IsBlending)
            {
                std::shared_ptr<Animation> prevAnim = assetManager->GetAnimation(component->LastAnimationName, skeletalMesh.get());

                
                
                
                
                float tps = currentAnim->getTicksPerSecond();
                if (!std::isfinite(tps) || tps <= 0.0f) tps = 25.0f;
                component->BlendTimer += tps * dt;

                float blendDurationTicks = component->BlendDuration * tps;
                if (component->BlendTimer >= blendDurationTicks)
                {
                    
                    component->IsBlending = false;
                    float leftover = component->BlendTimer - blendDurationTicks;
                    component->CurrentTime = (leftover > 0.0f) ? leftover : 0.0f;
                    
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
                    
                    component->IsBlending = false;
                    component->LastAnimationName = component->CurrentAnimationName;
                }
            }

            
            float tps = currentAnim->getTicksPerSecond();
            if (!std::isfinite(tps) || tps <= 0.0f) tps = 25.0f;
            component->CurrentTime += tps * dt * component->AnimationSpeed;
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
            
            std::fill(component->FinalBoneMatrices.begin(), component->FinalBoneMatrices.end(), glm::mat4(1.0f));
        }

    EndLoop:;
    }
}


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
        KeyPosition prevPos = prevBone->getPositions(component->HaltTime);
        KeyRotation prevRot = prevBone->getRotations(component->HaltTime);
        KeyScale    prevScl = prevBone->getScalings(component->HaltTime);

        KeyPosition nextPos = nextBone->getPositions(0.0f);
        KeyRotation nextRot = nextBone->getRotations(0.0f);
        KeyScale    nextScl = nextBone->getScalings(0.0f);

        float tps = nextAnim->getTicksPerSecond();
        if (!std::isfinite(tps) || tps <= 0.0f) tps = 25.0f;
        float blendDurationTicks = component->BlendDuration * tps;
        float alpha = (blendDurationTicks > 0.0f) ? Clamp01(component->BlendTimer / blendDurationTicks) : 1.0f;

        glm::vec3 pos = glm::mix(prevPos.position, nextPos.position, alpha);
        glm::quat a = prevRot.orientation;
        glm::quat b = nextRot.orientation;
        if (glm::dot(a, b) < 0.0f) b = -b;
        glm::quat rot = glm::normalize(glm::slerp(a, b, alpha));
        glm::vec3 scl = glm::mix(prevScl.scale, nextScl.scale, alpha);

        nodeTransform = glm::translate(glm::mat4(1.0f), pos) *
            glm::toMat4(rot) *
            glm::scale(glm::mat4(1.0f), scl);
    }

    glm::mat4 globalTransformation = parentTransform * nodeTransform;

    auto it = std::find_if(skeletalMesh->boneInfoMap.begin(), skeletalMesh->boneInfoMap.end(),
        [&](const BoneProps& props) { return props.name == nodeName; });

    if (it != skeletalMesh->boneInfoMap.end()) {
        int index = (int)std::distance(skeletalMesh->boneInfoMap.begin(), it);
        
        
        if (index < (int)component->FinalBoneMatrices.size()) {
            glm::mat4 finalMat = globalTransformation * it->offset;
            component->FinalBoneMatrices[index] = IsFiniteMat4(finalMat) ? finalMat : glm::mat4(1.0f);
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
        
        
        
        if (boneIndex < (int)boneProps.size() &&
            boneIndex < (int)component->FinalBoneMatrices.size())
        {
            glm::mat4 finalMat = globalTransformation * boneProps[boneIndex].offset;
            component->FinalBoneMatrices[boneIndex] = IsFiniteMat4(finalMat) ? finalMat : glm::mat4(1.0f);
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


