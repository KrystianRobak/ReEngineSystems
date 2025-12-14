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
#include "OpenGLViewport.h"

#include "ReCamera.h"

REFSYSTEM()
class REFLECTION_API RenderOpenGL : public RenderSystem
{
public:

	REFVARIABLE()
	std::vector<std::string> ComponentsToRegister = { "Transform", "StaticMesh"};

	virtual void InitApi(Editor::IEngineEditorApi* engine, std::shared_ptr<AssetManagerApi> AssetManger = nullptr) override;

	virtual void InitRenderContext(IWindow* window) override;

	virtual IWindow* GetWindow() override;

	virtual IViewport* CreateViewport(int width, int height) override;

	void Update(float dt);

	void Cleanup();

	void RenderViewport(Camera* camera, Commander* commander);

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

private:
	// --- G-Buffer Resources (Pass 2 Targets) ---
	GLuint gBuffer;
	GLuint gPosition, gNormal, gAlbedoSpec; // Textures for geometry data
	GLuint rboDepth; // Depth buffer for geometry
	int currentWidth = 0, currentHeight = 0;

	// --- Shadow Map Resources (Pass 1 Targets) ---
	GLuint shadowMapFBO;
	GLuint shadowMapTexture;
	const unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;

	// --- Shaders ---
	std::unique_ptr<Shader> geometryPassShader; // Writes to G-Buffer
	std::unique_ptr<Shader> lightingPassShader; // Reads G-Buffer -> Final Color
	std::unique_ptr<Shader> shadowMapShader;    // Depth only pass

	// --- Screen Quad for Lighting Pass ---
	GLuint quadVAO = 0;
	GLuint quadVBO;
	void RenderQuad();

	// --- Helpers ---
	void InitGBuffer(int width, int height);
	void InitShadowMap();
};


extern "C" REFLECTION_API System* CreateSystem();
