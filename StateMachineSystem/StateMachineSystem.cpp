#include "StateMachineSystem.h"
#include "Components/Transform.h"
#include <thread>
#include <SkeletalMeshComponent.h>

// --- NEW: Includes for loading ---
#include <fstream>
#include <json/json.hpp>
#include <filesystem>
#include <iostream>

using json = nlohmann::json;

// Factory
extern "C" REFLECTION_API System* CreateSystem() {
    return new StateMachineSystem();
}

void StateMachineSystem::Update(float dt)
{
    for (auto const& entity : mEntities)
    {
        // 1. Get Components
        auto animGraph = static_cast<StateMachine*>(engine_->GetComponent(entity, "StateMachine"));
        auto skelMesh = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));

        if (!animGraph || !skelMesh) continue;

        // If still no resource, skip
        if (!animGraph->GraphResource) continue;

        std::string targetAnimPath = "";
        std::string targetAnimName = "";

        // --- Priority 1: Interrupt Slot (e.g. Attacks, Hits) ---
        if (animGraph->IsSlotPlaying)
        {
            animGraph->SlotTimeRemaining -= dt;
            targetAnimName = animGraph->SlotAnimName; // Slots usually play by name or cached path

            if (animGraph->SlotTimeRemaining <= 0.0f) animGraph->IsSlotPlaying = false;
        }
        // --- Priority 2: State Machine Logic ---
        else
        {
            auto& res = animGraph->GraphResource;

            // Failsafe: if ID is -1, try to reset to entry
            if (animGraph->CurrentNodeID == -1) animGraph->CurrentNodeID = res->EntryNodeID;

            // Find Current Node Object
            GraphNode* currentNode = nullptr;
            for (auto& n : res->Nodes) {
                if (n.ID == animGraph->CurrentNodeID) {
                    currentNode = &n;
                    break;
                }
            }

            if (currentNode)
            {
                // Check Transitions
                bool transitionHappened = false;
                for (const auto& trans : res->Transitions)
                {
                    if (trans.FromNodeID != animGraph->CurrentNodeID) continue;

                    if (CheckCondition(trans, animGraph))
                    {
                        animGraph->CurrentNodeID = trans.ToNodeID;
                        skelMesh->CurrentTime = 0.0f; // Reset animation time for new state

                        // Re-fetch the new current node
                        for (auto& n : res->Nodes) {
                            if (n.ID == animGraph->CurrentNodeID) {
                                currentNode = &n;
                                break;
                            }
                        }
                        transitionHappened = true;
                        break; // Only take one transition per frame
                    }
                }

                // Set Target Animation from the (possibly new) Current Node
                if (currentNode) {
                    targetAnimPath = currentNode->AnimationPath; // Use the path from Drag & Drop
                    targetAnimName = currentNode->Name;          // Fallback / Debug name
                }
            }
        }

        // --- Drive the Mesh Component ---
        // We prioritize the Path if it exists, otherwise we might rely on the Name (legacy)
        std::string finalAnim = targetAnimPath.empty() ? targetAnimName : targetAnimPath;

        if (!finalAnim.empty() && skelMesh->CurrentAnimationName != finalAnim)
        {
            skelMesh->CurrentAnimationName = finalAnim;

            // Only reset time if we aren't in a slot (Slots handle their own time usually, or we reset above)
            if (!animGraph->IsSlotPlaying) {
                skelMesh->CurrentTime = 0.0f;
            }
        }
    }
}

bool StateMachineSystem::CheckCondition(const GraphTransition& trans, StateMachine* comp)
{
    // If the variable doesn't exist in the entity's blackboard, fail safely
    if (comp->Blackboard.find(trans.ConditionParam) == comp->Blackboard.end()) return false;

    AnimVar val = comp->Blackboard[trans.ConditionParam];

    if (val.Type == AnimVarType::Float) {
        // Use FloatVal (matching Editor serialization)
        if (trans.Operation == ConditionOp::Greater) return val.fVal > trans.Threshold;
        if (trans.Operation == ConditionOp::Less)    return val.fVal < trans.Threshold;
        if (trans.Operation == ConditionOp::Equal)   return std::abs(val.fVal - trans.Threshold) < 0.001f;
        if (trans.Operation == ConditionOp::NotEqual) return std::abs(val.fVal - trans.Threshold) > 0.001f;
    }
    else if (val.Type == AnimVarType::Bool) {
        // Use BoolVal
        bool target = (trans.Threshold > 0.5f);
        if (trans.Operation == ConditionOp::Equal)   return val.bVal == target;
        if (trans.Operation == ConditionOp::NotEqual) return val.bVal != target;
    }
    return false;
}