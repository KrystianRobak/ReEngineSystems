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
#include "Frustum.h"

#include "ReCamera.h"
#include <SkeletalMeshComponent.h>

REFSYSTEM()
class REFLECTION_API RenderOpenGL : public RenderSystem
{
public:

	REFVARIABLE()
		std::vector<std::string> ComponentsToRegister = { "Transform" };

	// --- DEPENDENCIES ---
	// Rendering waits for EVERYONE.
	REFVARIABLE()
		std::vector<std::string> SystemsToRunAfter = { "Animator", "Physics3D", "AiAssistant", "WorldGenSystem" };

	// Often Renderers must run on the Main Thread due to OpenGL context limitations
	REFVARIABLE()
		bool RunOnMainThread = false;

	virtual void InitApi(Editor::IEngineEditorApi* engine, IApplicationApi* application, std::shared_ptr<AssetManagerApi> AssetManger = nullptr) override;

	virtual void InitRenderContext(IWindow* window) override;

	virtual IWindow* GetWindow() override;

	virtual IViewport* CreateViewport(int width, int height) override;

	void Update(float dt);

	void Cleanup();

	void RenderViewport(Camera* camera, Commander* commander, FrameBuffer* framebuffer);

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

	std::vector<glm::vec3> mDebugQuadVertices;

	glm::mat4 projection;
	glm::mat4 view;

	Camera* currentCamera = nullptr;
public:
	// --- G-Buffer Resources (Pass 2 Targets) ---
	REFVARIABLE()
		GLuint gBuffer;
	REFVARIABLE()
		GLuint gPosition;
	REFVARIABLE()
		GLuint gNormal;
	REFVARIABLE()
		GLuint gAlbedoSpec;
	REFVARIABLE() // Textures for geometry data
		GLuint rboDepth; // Depth buffer for geometry
	REFVARIABLE()
		GLuint shadowMapTexture;
private:

	GLuint mDebugLineVAO = 0;
	GLuint mDebugLineVBO = 0;
	
    // Deferred Debug List
    std::vector<SkeletalMeshComponent*> mDebugSkeletonsToDraw;

	void RenderDebugSkeletons(const std::vector<SkeletalMeshComponent*>& components, Camera* camera);
	// A buffer to hold the model matrices for the current batch
	GLuint mInstanceVBO;

	// Grouping structure
	struct RenderBatch {
		std::shared_ptr<MeshResource> resource;
		int materialId;
		int subMeshIndex;
		std::vector<glm::mat4> instanceMatrices;

		std::vector<glm::mat4> boneMatrices;
	};

	// Helper to configure the matrix attributes on a VAO
	void SetupInstanceAttributes(GLuint vao);


	int currentWidth = 0, currentHeight = 0;

	// --- Shadow Map Resources (Pass 1 Targets) ---
	GLuint shadowMapFBO;

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
