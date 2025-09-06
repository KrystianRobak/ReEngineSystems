#pragma once

#include "Engine/Systems/Render/Shader.h"

#include <GL/glew.h>
#include "GLFW/glfw3.h"
#include <glm/glm.hpp>
#include "ReflectionCoreExport.h"
#include "System\System.h"
#include "ReflectionMacros.h"
#include "ReflectionEngine.h"
#include "Logger.h"

REFSYSTEM()
class REFLECTION_API RenderOpenGL : public System
{
public:

	void Update(float dt);

	void Cleanup();

	void SetupModelAndMesh(const Entity& entity);

	void OnLightEntityAdded();
	void RecompileShader();

private:
	void WindowSizeListener();

	std::unique_ptr<Shader> shader;
	std::unique_ptr<Shader> LightShader;
	std::unique_ptr<Shader> AABBshader;

	GLuint mVao{};
	GLuint mVboVertices{};
	GLuint mVboNormals{};

	glm::mat4 projection;
	glm::mat4 view;
};


extern "C" REFLECTION_API System* CreateSystem();
