#include "StateMachineSystem.h"
#include "Components/Transform.h"
#include <thread>
#include <SkeletalMeshComponent.h>

// --- NEW: Includes for loading ---
#include <fstream>
#include <json/json.hpp>
#include <filesystem>
#include <iostream>
#include "Api/AssetManagerApi.h"

using json = nlohmann::json;

// Factory
extern "C" REFLECTION_API System* CreateSystem() {
    return new StateMachineSystem();
}

void StateMachineSystem::Update(float dt)
{
    static bool debugSM = false;

    for (auto const& entity : mEntities)
    {
        // 1. Get Components
        auto animGraph = static_cast<StateMachine*>(engine_->GetComponent(entity, "StateMachine"));
        auto skelMesh = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(entity, "SkeletalMeshComponent"));

        if (!animGraph || !skelMesh) continue;

        // --- LAZY LOAD RESOURCE ---
        if (!animGraph->GraphResource && !animGraph->GraphAssetPath.empty()) {
            std::cout << "[StateMachine] Lazy loading graph for Entity " << entity << ": " << animGraph->GraphAssetPath << "\n";
            animGraph->GraphResource = engine_->GetAssetManager()->GetAnimationGraph(animGraph->GraphAssetPath);

            // Initialize Defaults if loaded
            if (animGraph->GraphResource) {
                if (animGraph->CurrentNodeID == -1) {
                    animGraph->CurrentNodeID = animGraph->GraphResource->EntryNodeID;
                    std::cout << "[StateMachine] Set initial node to Entry: " << animGraph->CurrentNodeID << "\n";
                }

                // Copy defaults if not present
                for (auto& [key, val] : animGraph->GraphResource->DefaultBlackboard) {
                    if (animGraph->Blackboard.find(key) == animGraph->Blackboard.end()) {
                        animGraph->Blackboard[key] = val;
                    }
                }
            }
            else {
                std::cerr << "[StateMachine] Failed to load graph resource!\n";
            }
        }

        // If still no resource, skip
        if (!animGraph->GraphResource) continue;

        std::string targetAnimPath = "";
        std::string targetAnimName = "";
        bool loop = true;

        // --- Priority 1: Interrupt Slot (e.g. Attacks, Hits) ---
        if (animGraph->IsSlotPlaying)
        {
            animGraph->SlotTimeRemaining -= dt;
            targetAnimName = animGraph->SlotAnimName; // Slots usually play by name or cached path
            loop = false; // Slots are usually one-shot

            if (animGraph->SlotTimeRemaining <= 0.0f) {
                animGraph->IsSlotPlaying = false;
                //std::cout << "[StateMachine] Slot finished. Returning to graph.\n";
            }
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
                for (const auto& trans : res->Transitions)
                {
                    if (trans.FromNodeID != animGraph->CurrentNodeID) continue;

                    if (CheckCondition(trans, animGraph))
                    {
                        //std::cout << "[StateMachine] Transition: " << currentNode->Name << " -> Node " << trans.ToNodeID << "\n";
                        animGraph->CurrentNodeID = trans.ToNodeID;
                        skelMesh->CurrentTime = 0.0f; // Reset animation time for new state

                        // Re-fetch the new current node
                        for (auto& n : res->Nodes) {
                            if (n.ID == animGraph->CurrentNodeID) {
                                currentNode = &n;
                                break;
                            }
                        }
                        break; // Only take one transition per frame
                    }
                }

                // Set Target Animation from the (possibly new) Current Node
                if (currentNode) {
                    targetAnimPath = currentNode->AnimationPath; // Use the path from Drag & Drop
                    targetAnimName = currentNode->Name;          // Fallback
                    loop = currentNode->IsLooping;
                }
            }
            else {
                if (!debugSM) {
                    std::cerr << "[StateMachine] Invalid CurrentNodeID: " << animGraph->CurrentNodeID << "\n";
                    debugSM = true;
                }
            }
        }

        // --- Drive the Mesh Component ---
        // Prioritize Path. If Path is empty, use Name (might be a keyword or fallback).
        std::string finalAnim = targetAnimPath.empty() ? targetAnimName : targetAnimPath;

        if (!finalAnim.empty())
        {
            // Only update if changed to prevent constant resets (unless we need to force re-trigger)
            if (skelMesh->CurrentAnimationName != finalAnim)
            {
                std::cout << "[StateMachine] Switch Anim: '" << skelMesh->CurrentAnimationName << "' -> '" << finalAnim << "'\n";
                skelMesh->CurrentAnimationName = finalAnim;
                
                // Reset time when switching animations (unless we specifically want blending, which isn't impl yet)
                // Note: If we just finished a slot, we might want to blend back to the graph state's current time?
                // For now, hard reset is safer to ensure it starts playing.
                skelMesh->CurrentTime = 0.0f; 
            }

            skelMesh->IsLooping = loop;
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