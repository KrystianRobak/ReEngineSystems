#include "RenderOpenGL.h"

#include "Components/Transform.h"

#include <thread>
#include "MeshData.h"
#include "StaticMesh.h"
#include "StaticMeshData.h"
#include "MaterialSystem/Material.h"

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

void RenderOpenGL::InitRenderContext(IWindow* window)
{
    shader = std::make_unique<Shader>("shaders/LightSourceShader/LightSources.vs", "shaders/LightSourceShader/LightSources.fs");
}

IWindow* RenderOpenGL::GetWindow()
{
    return new Window();
}

IViewport* RenderOpenGL::CreateViewport(int width, int height)
{
    return new OpenGLViewport(width, height);
}

void RenderOpenGL::Update(float dt)
{
}

void RenderOpenGL::RenderViewport(Camera* camera, Commander* commander)
{
    assetManager_->UploadPendingResources();
    std::vector<RenderCommand> RenderCommands = commander->ConsumeRenderCommands();

    glm::mat4 view = ReCamera::GetViewMatrix(*camera);
    glm::mat4 projection = ReCamera::GetProjectionMatrix(*camera);

    for (const RenderCommand& command : RenderCommands)
    {
        const RenderPrimitive& prim = command.Primitive;

        if (command.Primitive.MaterialId == 1200)
        {
            // ... existing default shader logic ...
            shader->Use();
            shader->SetMat4("View", view);
            shader->SetMat4("Projection", projection);
            shader->SetMat4("Model", prim.ModelMatrix);
        }
        else
        {
            CompiledMaterial* material = assetManager_->GetMaterial(command.Primitive.MaterialId);
            if (!material || !material->GLShader) continue;

            material->GLShader->Use();
            material->GLShader->SetMat4("View", view);
            material->GLShader->SetMat4("Projection", projection);
            material->GLShader->SetMat4("Model", prim.ModelMatrix);

            // 1. Set Uniform Parameters (Floats/Vecs)
            for (auto& [name, value] : material->m_Parameters)
            {
                material->GLShader->SetVec4(name, value);
            }

            // 2. --- NEW TEXTURE BINDING LOGIC ---
            int textureSlot = 0;
            for (auto& [samplerName, textureResource] : material->textures)
            {
                if (textureResource && textureResource->uploaded)
                {
                    // Activate proper texture unit (0, 1, 2...)
                    glActiveTexture(GL_TEXTURE0 + textureSlot);

                    // Bind the GPU Texture ID
                    glBindTexture(GL_TEXTURE_2D, textureResource->id);

                    // Tell the shader that 'samplerName' (e.g., "Tex_S1_Tex") 
                    // should look at texture unit 'textureSlot'
                    material->GLShader->SetInt(samplerName, textureSlot);

                    textureSlot++;
                }
            }
            // -------------------------------------
        }

        MeshResource* res = assetManager_->GetMeshResource(prim.MeshResourceId);
        if (!res || !res->uploaded) continue;

        glBindVertexArray(res->VAO);
        glDrawElements(GL_TRIANGLES, res->indexCount, GL_UNSIGNED_INT, 0);
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
