#pragma once

#include "ReflectionCoreExport.h"
#include "System\System.h"
#include "ReflectionMacros.h"
#include "ReflectionEngine.h"
#include "Logger.h"

REFSYSTEM()
class REFLECTION_API Physics2D : public System
{
public:

	void Init(Engine::IEngineApi* engine);

	void Update(float dt);

	void Cleanup();

	void SetupModelAndMesh(const Entity& entity);

	void OnLightEntityAdded();
	void RecompileShader();

private:
	void WindowSizeListener();
};


extern "C" REFLECTION_API System* CreateSystem();
