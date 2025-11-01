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
class REFLECTION_API Physics3D : public System
{
public:

	REFVARIABLE()
	std::vector<std::string> ComponentsToRegister = {"Transform"};

	void Update(float dt);

	void Cleanup();

	REFFUNCTION()
	void AddForceToEntity(Entity entity, int xForce);

	void SetupModelAndMesh(const Entity& entity);

	void OnLightEntityAdded();
	void RecompileShader();

private:
	void WindowSizeListener();

	std::vector<std::tuple<Entity, int>> AppliedForces;
};


extern "C" REFLECTION_API System* CreateSystem();
