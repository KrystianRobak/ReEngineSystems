#pragma once

#include <GL/glew.h>
#include "GLFW/glfw3.h"
#include <glm/glm.hpp>
#include "ReflectionCoreExport.h"
#include "RenderSystem.h"
#include "ReflectionMacros.h"
#include "ReflectionEngine.h"
#include "Logger.h"
#include "Window.h"
#include "Shader.h"

#include "ReCamera.h"

#include "../Commander.h"

REFSYSTEM()
class REFLECTION_API RenderOpenGL : public RenderSystem
{
public:

	REFVARIABLE()
	std::vector<std::string> ComponentsToRegister = { "Transform", "StaticMesh"};

	virtual void InitApi(Editor::IEngineEditorApi* engine, std::shared_ptr<AssetManagerApi> AssetManger = nullptr) override;

	virtual void InitRenderContext(GLFWwindow* window) override;

	virtual IWindow* GetWindow() override;

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

	Camera* currentCamera = nullptr;


};


extern "C" REFLECTION_API System* CreateSystem();
