#include "StateMachineSystem.h"

#include "Components/Transform.h"
#include <thread>
#include <SkeletalMeshComponent.h>


// Factory
extern "C" REFLECTION_API System* CreateSystem() {
    return new StateMachineSystem();
}

void StateMachineSystem::Update(float dt)
{
    for (auto const& entity : mEntities)
    {
        // 1. Get Components
        auto animGraph = static_cast<StateMachine*>(engine_->GetComponent(entity, "AnimationGraphComponent"));
        auto skelMesh = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));

        if (!animGraph || !skelMesh) continue;

        // Lazy Load (Placeholder)
        if (!animGraph->GraphResource && !animGraph->GraphAssetPath.empty()) {
            // In real code: animGraph->GraphResource = AssetManager->LoadGraph(animGraph->GraphAssetPath);
            // animGraph->Blackboard = animGraph->GraphResource->DefaultBlackboard; // Copy defaults
            continue;
        }
        if (!animGraph->GraphResource) continue;

        std::string targetAnimation = "";

        // --- Priority 1: Interrupt Slot ---
        if (animGraph->IsSlotPlaying)
        {
            animGraph->SlotTimeRemaining -= dt;
            targetAnimation = animGraph->SlotAnimName;

            if (animGraph->SlotTimeRemaining <= 0.0f) animGraph->IsSlotPlaying = false;
        }
        // --- Priority 2: State Machine ---
        else
        {
            auto& res = animGraph->GraphResource;

            // Initialize Entry
            if (animGraph->CurrentNodeID == -1) animGraph->CurrentNodeID = res->EntryNodeID;

            // Find Current Node
            GraphNode* currentNode = nullptr;
            for (auto& n : res->Nodes) { if (n.ID == animGraph->CurrentNodeID) currentNode = &n; }

            if (currentNode)
            {
                // Check Transitions
                for (const auto& trans : res->Transitions)
                {
                    if (trans.FromNodeID != animGraph->CurrentNodeID) continue;

                    if (CheckCondition(trans, animGraph))
                    {
                        animGraph->CurrentNodeID = trans.ToNodeID;
                        skelMesh->CurrentTime = 0.0f; // Reset anim time

                        // Re-fetch node
                        for (auto& n : res->Nodes) { if (n.ID == animGraph->CurrentNodeID) currentNode = &n; }
                        break;
                    }
                }
                if (currentNode) targetAnimation = currentNode->AnimationName;
            }
        }

        // --- Drive Mesh ---
        if (!targetAnimation.empty() && skelMesh->CurrentAnimationName != targetAnimation)
        {
            skelMesh->CurrentAnimationName = targetAnimation;
            if (!animGraph->IsSlotPlaying) skelMesh->CurrentTime = 0.0f;
        }
    }
}

bool StateMachineSystem::CheckCondition(const GraphTransition& trans, StateMachine* comp)
{
    if (comp->Blackboard.find(trans.ConditionParam) == comp->Blackboard.end()) return false;
    AnimVar val = comp->Blackboard[trans.ConditionParam];

    if (val.Type == AnimVarType::Float) {
        if (trans.Operation == ConditionOp::Greater) return val.fVal > trans.Threshold;
        if (trans.Operation == ConditionOp::Less)    return val.fVal < trans.Threshold;
    }
    else if (val.Type == AnimVarType::Bool) {
        bool target = (trans.Threshold > 0.5f);
        if (trans.Operation == ConditionOp::Equal) return val.bVal == target;
    }
    return false;
}