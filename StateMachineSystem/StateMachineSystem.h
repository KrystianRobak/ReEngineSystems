#pragma once

#include <GL/glew.h>
#include "GLFW/glfw3.h"
#include <glm/glm.hpp>
#include "ReflectionCoreExport.h"
#include "System\System.h"
#include "ReflectionMacros.h"
#include "ReflectionEngine.h"
#include "Logger.h"

#include "../Commander.h"
#include <StateMachine.h>

REFSYSTEM()
class REFLECTION_API StateMachineSystem : public System
{
public:

	REFVARIABLE()
	std::vector<std::string> ComponentsToRegister = {"StateMachine", "SkeletalMeshComponent" };

	void Update(float dt);

	void Cleanup();

	bool CheckCondition(const GraphTransition& trans, StateMachine* comp);
};


extern "C" REFLECTION_API System* CreateSystem();
