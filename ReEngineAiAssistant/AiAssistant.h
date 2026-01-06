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
class REFLECTION_API AiAssistant : public System
{
public:

	REFVARIABLE()
		std::vector<std::string> ComponentsToRegister = {};

	REFVARIABLE()
		std::vector<std::string> SystemsToRunAfter = {};

	void Update(float dt);

	void Cleanup();

	REFFUNCTION()
	void CreateUi();

private:

};


extern "C" REFLECTION_API System* CreateSystem();
