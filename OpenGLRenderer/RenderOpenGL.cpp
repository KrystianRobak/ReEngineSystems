#include "RenderOpenGL.h"

#include "Components/Transform.h"

#include <thread>
#include "MeshData.h"
#include "StaticMesh.h"
#include "StaticMeshData.h"
#include "MaterialSystem/Material.h"

#include "ReScene.h"


bool IsEntityVisible(const Frustum& frustum, const glm::mat4& modelMatrix, const glm::vec3& localMin, const glm::vec3& localMax)
{
    // Fast algorithm to transform an AABB into World Space
    // Source: "Transforming Axis-Aligned Bounding Boxes" by Jim Arvo

    glm::vec3 globalCenter = glm::vec3(modelMatrix[3]); // Translation part

    glm::vec3 right = glm::vec3(modelMatrix[0]);
    glm::vec3 up = glm::vec3(modelMatrix[1]);
    glm::vec3 forward = glm::vec3(modelMatrix[2]);

    glm::vec3 localCenter = (localMin + localMax) * 0.5f;
    glm::vec3 localExtents = (localMax - localMin) * 0.5f;

    // Transform center
    glm::vec3 newCenter = globalCenter +
        (right * localCenter.x) +
        (up * localCenter.y) +
        (forward * localCenter.z);

    // Transform extents (take absolute value of rotation to fit the box)
    glm::vec3 newExtents = glm::vec3(
        std::abs(right.x) * localExtents.x + std::abs(up.x) * localExtents.y + std::abs(forward.x) * localExtents.z,
        std::abs(right.y) * localExtents.x + std::abs(up.y) * localExtents.y + std::abs(forward.y) * localExtents.z,
        std::abs(right.z) * localExtents.x + std::abs(up.z) * localExtents.y + std::abs(forward.z) * localExtents.z
    );

    glm::vec3 worldMin = newCenter - newExtents;
    glm::vec3 worldMax = newCenter + newExtents;

    return frustum.IsBoxVisible(worldMin, worldMax);
}


static inline void glfw_error_callback(int error, const char* description)
{
    LOGF_ERROR("GLFW Error %d : %s", error, description)
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

    geometryPassShader = std::make_unique<Shader>("shaders/Deferred/GBuffer.vs", "shaders/Deferred/GBuffer.fs");
    lightingPassShader = std::make_unique<Shader>("shaders/Deferred/DefferedLight.vs", "shaders/Deferred/DefferedLight.fs");
    shadowMapShader = std::make_unique<Shader>("shaders/Deferred/ShadowMap.vs", "shaders/Deferred/ShadowMap.fs");

    InitShadowMap();
    InitGBuffer(1920, 1080); // Initial size, should match viewport

    glGenBuffers(1, &mInstanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
    // Reserve space for ~10,000 instances (640KB), dynamic usage
    glBufferData(GL_ARRAY_BUFFER, 10000 * sizeof(glm::mat4), nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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

void RenderOpenGL::RenderViewport(Camera* camera, Commander* commander, FrameBuffer* framebuffer)
{


    glm::mat4 view = ReCamera::GetViewMatrix(*camera);
    glm::mat4 projection = ReCamera::GetProjectionMatrix(*camera);

    Frustum cameraFrustum;
    cameraFrustum.Update(projection * view);

    std::vector<RenderCommand> RenderCommands = commander->ConsumeRenderCommands();

    std::vector<RenderBatch> batches;

    // --- CULLING & BATCHING ---
    for (const auto& cmd : RenderCommands) {
        auto& res = cmd.Primitive.Mesh;
        if (!res || !res->uploaded || !res->cpuMesh) continue;

        // 1. BROAD PHASE: Check the whole object first
        // If the entire building is behind us, don't waste time checking windows and doors.
        if (!IsEntityVisible(cameraFrustum, cmd.Primitive.ModelMatrix, res->cpuMesh->aabbMin, res->cpuMesh->aabbMax)) {
            continue;
        }

        // 2. NARROW PHASE: Check each sub-mesh
        for (size_t i = 0; i < res->cpuMesh->meshes.size(); ++i) {
            const auto& subMesh = res->cpuMesh->meshes[i];

            // Check if this specific part is visible
            // Note: If subMesh has a LocalTransform, you must combine it: ModelMatrix * LocalTransform
            glm::mat4 effectiveModelMatrix = cmd.Primitive.ModelMatrix; // * subMesh.LocalTransform;

            if (IsEntityVisible(cameraFrustum, effectiveModelMatrix, subMesh.aabbMin, subMesh.aabbMax)) {

                // FIND OR CREATE BATCH
                bool batchFound = false;
                for (auto& batch : batches) {
                    if (batch.resource == res &&
                        batch.subMeshIndex == i && // Match specific sub-mesh
                        batch.materialId == cmd.Primitive.MaterialId)
                    {
                        batch.instanceMatrices.push_back(effectiveModelMatrix);
                        batchFound = true;
                        break;
                    }
                }

                if (!batchFound) {
                    RenderBatch newBatch;
                    newBatch.resource = res;
                    newBatch.subMeshIndex = i;
                    newBatch.materialId = cmd.Primitive.MaterialId;
                    newBatch.instanceMatrices.push_back(effectiveModelMatrix);
                    batches.push_back(newBatch);
                }
            }
        }
    }



    // === Define Light Variables (Todo: Get from Commander/ECS) ===
    glm::vec3 lightPos(-20.0f, 50.0f, -10.0f); // Directional Light Source
    glm::mat4 lightProjection = glm::ortho(-40.0f, 40.0f, -40.0f, 40.0f, 1.0f, 100.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    // ====================================================
    // PASS 1: Shadow Map Generation
    // ====================================================
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO);
    glClear(GL_DEPTH_BUFFER_BIT);

    shadowMapShader->Use();
    shadowMapShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);

    // Render Scene (Depth Only)
    for (auto& batch : batches) {
        if (batch.instanceMatrices.empty()) continue;

        glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
        // Upload the matrices for THIS batch only
        glBufferData(GL_ARRAY_BUFFER, batch.instanceMatrices.size() * sizeof(glm::mat4), batch.instanceMatrices.data(), GL_STREAM_DRAW);

        GLuint vao = batch.resource->VAOs[batch.subMeshIndex];
        uint32_t count = batch.resource->indexCounts[batch.subMeshIndex];

        glBindVertexArray(vao);
        SetupInstanceAttributes(vao); // Point attributes to mInstanceVBO

        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, (GLsizei)batch.instanceMatrices.size());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ====================================================
    // PASS 2: Geometry Pass (G-Buffer)
    // ====================================================
    glViewport(0, 0, currentWidth, currentHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    geometryPassShader->Use();
    geometryPassShader->SetMat4("view", view);
    geometryPassShader->SetMat4("projection", projection);

    for (auto& batch : batches) {
        if (batch.instanceMatrices.empty()) continue;

        // A. Upload Matrices to GPU
        // Bind the huge instance buffer
        glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
        // Upload the matrices for THIS batch only
        glBufferData(GL_ARRAY_BUFFER, batch.instanceMatrices.size() * sizeof(glm::mat4), batch.instanceMatrices.data(), GL_STREAM_DRAW);


        CompiledMaterial* mat = assetManager_->GetMaterial(batch.materialId);

        Shader* currentShader = nullptr;

        // 1. SELECT SHADER
        if (mat && mat->GLShader) {
            // Use the shader generated by your Node Graph (Modified in Part 1)
            currentShader = mat->GLShader;
            currentShader->Use();

            for (auto& [name, value] : mat->m_Parameters) {
                currentShader->SetVec4(name, value);
            }

            // Bind Textures
            int texSlot = 0;
            for (auto& [samplerName, texRes] : mat->textures) {
                if (texRes && texRes->uploaded) {
                    glActiveTexture(GL_TEXTURE0 + texSlot);
                    glBindTexture(GL_TEXTURE_2D, texRes->id);
                    currentShader->SetInt(samplerName, texSlot);
                    texSlot++;
                }
            }
        }
        else {
            // Fallback to the default GBuffer shader provided above
            currentShader = geometryPassShader.get();
            currentShader->Use();

            currentShader->SetVec3("uAlbedo", glm::vec3(0.8f, 0.8f, 0.8f));
            currentShader->SetFloat("uMetallic", 0.0f);
            currentShader->SetFloat("uRoughness", 0.5f);
            currentShader->SetBool("uUseTextures", false);
        }

        

        // 2. SET COMMON UNIFORMS
        currentShader->SetMat4("view", view);
        currentShader->SetMat4("projection", projection);

        // 3. SET MATERIAL UNIFORMS (Graph Parameters)
        if (mat && mat->GLShader) {
            // Apply Float/Vec parameters from the graph
            for (auto& [name, value] : mat->m_Parameters) {
                currentShader->SetVec4(name, value);
            }

            // Bind Textures from the graph
            int texSlot = 0;
            for (auto& [samplerName, texRes] : mat->textures) {
                if (texRes && texRes->uploaded) {
                    glActiveTexture(GL_TEXTURE0 + texSlot);
                    glBindTexture(GL_TEXTURE_2D, texRes->id);
                    currentShader->SetInt(samplerName, texSlot);
                    texSlot++;
                }
            }
        }

        // 4. DRAW
        GLuint vao = batch.resource->VAOs[batch.subMeshIndex];
        uint32_t count = batch.resource->indexCounts[batch.subMeshIndex];

        glBindVertexArray(vao);
        SetupInstanceAttributes(vao); // Point attributes to mInstanceVBO

        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, (GLsizei)batch.instanceMatrices.size());
    }


    framebuffer->bind();


    // ====================================================
    // PASS 3: Lighting Pass (Deferred)
    // ====================================================
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    lightingPassShader->Use();

    // Bind G-Buffer Textures for Reading
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    lightingPassShader->SetInt("gPosition", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    lightingPassShader->SetInt("gNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
    lightingPassShader->SetInt("gAlbedoSpec", 2);

    // Bind Shadow Map
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
    lightingPassShader->SetInt("shadowMap", 3);

    // Set Lighting Uniforms
    lightingPassShader->SetVec3("viewPos", camera->CameraTransform.position);
    lightingPassShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
    lightingPassShader->SetVec3("lightPos", lightPos);
    lightingPassShader->SetVec3("lightColor", glm::vec3(1.0, 0.95, 0.9));

    glDisable(GL_DEPTH_TEST); // Ensure quad draws over everything
    RenderQuad();             // This now draws into your UI texture
    glEnable(GL_DEPTH_TEST);

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

void RenderOpenGL::SetupInstanceAttributes(GLuint vao)
{
    // Bind the buffer containing the matrices (It must be bound to GL_ARRAY_BUFFER)
    glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);

    // Matrix is 4 vec4s. We need attributes 5, 6, 7, 8.
    // Assuming sizeof(glm::mat4) == 64 bytes
    GLsizei vec4Size = sizeof(glm::vec4);

    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(5 + i);
        glVertexAttribPointer(5 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));

        // THIS IS THE KEY: 1 = Update per instance
        glVertexAttribDivisor(5 + i, 1);
    }
}

void RenderOpenGL::RenderQuad()
{
    if (quadVAO == 0)
    {
        float quadVertices[] = {
            // positions        // texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void RenderOpenGL::InitGBuffer(int width, int height)
{
    currentWidth = width;
    currentHeight = height;

    glGenFramebuffers(1, &gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

    // 1. Position Buffer (High Precision Float for world coordinates)
    glGenTextures(1, &gPosition);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition, 0);

    // 2. Normal Buffer (Float precision for smooth lighting)
    glGenTextures(1, &gNormal);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);

    // 3. Albedo + Specular Buffer (Standard Color)
    glGenTextures(1, &gAlbedoSpec);
    glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoSpec, 0);

    // Tell OpenGL to draw to these 3 attachments
    unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, attachments);

    // 4. Depth Buffer (Standard Renderbuffer)
    glGenRenderbuffers(1, &rboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Framebuffer not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderOpenGL::InitShadowMap()
{
    glGenFramebuffers(1, &shadowMapFBO);

    glGenTextures(1, &shadowMapTexture);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
    // Depth component only, 2048x2048 resolution
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Clamp to border to prevent artifacts outside shadow map
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

extern "C" {
	REFLECTION_API System* CreateSystem()
	{
		return new RenderOpenGL();
	}

}
