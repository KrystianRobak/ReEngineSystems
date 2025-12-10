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

REFSYSTEM()
class REFLECTION_API Animator : public System
{
public:

	REFVARIABLE()
	std::vector<std::string> ComponentsToRegister = {"SkeletalMeshComponent"};

	void Update(float dt);

	void Cleanup();


private:
};


extern "C" REFLECTION_API System* CreateSystem();
