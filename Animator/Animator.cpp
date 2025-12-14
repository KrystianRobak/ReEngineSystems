#include "Animator.h"
#include <SkeletalMeshComponent.h>
#include <iostream>
#include <string>

#include "../../ReEngine/ReEngine/Public/Engine/Animation/Animation.h"
#include "../../ReEngine/ReEngine/Public/Engine/Animation/AssimpNodeData.h"

void Animator::Update(float dt)
{
	for (auto const& entity : mEntities)
	{
		auto component = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));
		if (!component || !component->MeshResource) continue;

		if (component->MeshResource->animations.empty()) continue;

		Animation* currentAnimation = nullptr;
		
        // Use the selected animation if available, otherwise default to the first one
		if (!component->CurrentAnimationName.empty())
		{
            auto it = component->MeshResource->animations.find(component->CurrentAnimationName);
            if(it != component->MeshResource->animations.end())
			    currentAnimation = &it->second;
		}
        
        if (!currentAnimation && !component->MeshResource->animations.empty())
		{
			auto it = component->MeshResource->animations.begin();
			currentAnimation = &it->second;
            // Update the component to reflect the playing animation
            component->CurrentAnimationName = currentAnimation->GetName();
		}

		if (currentAnimation)
		{
			component->CurrentTime += currentAnimation->GetTicksPerSecond() * dt * component->AnimationSpeed;
			component->CurrentTime = fmod(component->CurrentTime, currentAnimation->GetDuration());

			CalculateBoneTransform(&currentAnimation->GetRootNode(), glm::mat4(1.0f), component, currentAnimation);
		}
	}
}

void Animator::CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform, SkeletalMeshComponent* component, Animation* animation)
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

	auto& boneInfoMap = component->MeshResource->boneInfoMap;
	if (boneInfoMap.find(nodeName) != boneInfoMap.end())
	{
		int index = boneInfoMap[nodeName].id;
		glm::mat4 offset = boneInfoMap[nodeName].offset;
        
        // Ensure the vector is large enough
        if(component->FinalBoneMatrices.size() <= index) {
            component->FinalBoneMatrices.resize(index + 1, glm::mat4(1.0f));
        }

		component->FinalBoneMatrices[index] = globalTransformation * offset;
	}

	for (int i = 0; i < node->childrenCount; i++)
		CalculateBoneTransform(&node->children[i], globalTransformation, component, animation);
}

void Animator::Cleanup()
{
}

extern "C" REFLECTION_API System* CreateSystem()
{
	return new Animator();
}