#include "RenderOpenGL.h"

#include "Components/Transform.h"

#include <thread>
#include "MeshData.h"
#include "StaticMesh.h"
#include "StaticMeshData.h"

#include "ReScene.h"

static inline void glfw_error_callback(int error, const char* description)
{
    LOGF_ERROR("GLFW Error %d : %s", error, description)
}

void SetupMesh(MeshData& mesh) {

	if (mesh.isSetup) return;

	mesh.isSetup = true;

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(uint32_t), mesh.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Position));

    glEnableVertexAttribArray(1); // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

    glEnableVertexAttribArray(2); // TexCoords
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    glEnableVertexAttribArray(3); // Tangent
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));

    glEnableVertexAttribArray(4); // Bitangent
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

    glBindVertexArray(0);
}

void RenderOpenGL::InitApi(Editor::IEngineEditorApi* engine, std::shared_ptr<AssetManagerApi> AssetManger)
{
    engine_ = engine;

    assetManager_ = std::shared_ptr<AssetManagerApi>(AssetManger);

    currentCamera = engine_->GetCurrentScene()->GetDefaultCamera();
}

void RenderOpenGL::InitRenderContext(GLFWwindow* window)
{
    shader = std::make_unique<Shader>("shaders/LightSourceShader/LightSources.vs", "shaders/LightSourceShader/LightSources.fs");
}

IWindow* RenderOpenGL::GetWindow()
{
    return new Window();
}

FrameBuffer* RenderOpenGL::CreateFramebuffer(int32_t width, int32_t height)
{
    OpenGlFrameBuffer* newBuffer = new OpenGlFrameBuffer();
    newBuffer->create_buffers(width, height);
    return newBuffer;
}

void RenderOpenGL::RenderViewport(Camera* camera, Commander* commander)
{
    // Safety check
    if (!camera || !commander) return;

    // Consume commands from the *provided* commander
    std::vector<RenderCommand> RenderCommands = commander->ConsumeRenderCommands();

    shader->Use();

    // Use the *provided* camera
    glm::mat4 view = ReCamera::GetViewMatrix(*camera);
    glm::mat4 projection = ReCamera::GetProjectionMatrix(*camera);

    shader->SetMat4("View", view);
    shader->SetMat4("Projection", projection);

    for (RenderCommand command : RenderCommands)
    {
        auto commandPrimitive = command.Primitive;
        auto transform = (Transform)commandPrimitive.transform;
        Entity entity = commandPrimitive.entity;

        glm::mat4 model = ReCamera::GetModelMatrix(transform);
        shader->SetMat4("Model", model);

        // Fetch StaticMeshData from entity
        auto staticMesh = static_cast<StaticMesh*>(engine_->GetComponent(entity, "StaticMesh"));
        if (!staticMesh) continue;

        for (auto& mesh : staticMesh->StaticMeshHandler->meshes) {
            SetupMesh(mesh);
        }

        for (auto& mesh : staticMesh->StaticMeshHandler->meshes) {
            glBindVertexArray(mesh.VAO);
            glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void RenderOpenGL::Update(float dt)
{


    std::vector<RenderCommand> RenderCommands = commander_->ConsumeRenderCommands();

    shader->Use();

    glm::mat4 view = ReCamera::GetViewMatrix(*currentCamera);

	glm::mat4 projection = ReCamera::GetProjectionMatrix(*currentCamera);


    shader->SetMat4("View", view);
    shader->SetMat4("Projection", projection);


    for (RenderCommand command : RenderCommands)
    {
        auto commandPrimitive = command.Primitive;
        auto transform = commandPrimitive.transform;
        Entity entity = commandPrimitive.entity;

        glm::mat4 model = ReCamera::GetModelMatrix(transform);
        shader->SetMat4("Model", model);

        // Fetch StaticMeshData from entity
        auto staticMesh = static_cast<StaticMesh*>(engine_->GetComponent(entity, "StaticMesh"));
        if (!staticMesh) continue;

        for (auto& mesh : staticMesh->StaticMeshHandler->meshes) {
            SetupMesh(mesh);
        }

        for (auto& mesh : staticMesh->StaticMeshHandler->meshes) {
            glBindVertexArray(mesh.VAO);
            glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void RenderOpenGL::Cleanup()
{
}

void RenderOpenGL::SetupModelAndMesh(const Entity& entity)
{
}

void RenderOpenGL::OnLightEntityAdded()
{
}

void RenderOpenGL::RecompileShader()
{
}

void RenderOpenGL::WindowSizeListener()
{
}

extern "C" {
	REFLECTION_API System* CreateSystem()
	{
		return new RenderOpenGL();
	}

}
