#include "RenderOpenGL.h"

#include "Components/Transform.h"

#include <thread>
#include "MeshData.h"
#include "StaticMesh.h"
#include "StaticMeshData.h"
#include "MaterialSystem/Material.h"

#include "ReScene.h"
#include <gtc/type_ptr.hpp>


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

    glGenVertexArrays(1, &mDebugLineVAO);
    glGenBuffers(1, &mDebugLineVBO);

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

    const std::vector<RenderCommand>& RenderCommands = commander->ConsumeRenderCommands();
    std::vector<RenderBatch> batches;

    // Clear previous debug queue
    mDebugSkeletonsToDraw.clear();

    // --- 1. CULLING & BATCHING ---
    for (const auto& cmd : RenderCommands) {
        auto& res = cmd.Primitive.Mesh;
        if (!res || !res->uploaded || !res->cpuMesh) continue;

        // Frustum Culling
        if (!IsEntityVisible(cameraFrustum, cmd.Primitive.ModelMatrix, res->cpuMesh->aabbMin, res->cpuMesh->aabbMax)) {
            continue;
        }

        bool isSkeletal = !cmd.Primitive.FinalBoneMatrices.empty();

        for (size_t i = 0; i < res->cpuMesh->meshes.size(); ++i) {
            const auto& subMesh = res->cpuMesh->meshes[i];
            glm::mat4 effectiveModelMatrix = cmd.Primitive.ModelMatrix;

            // Submesh Culling
            if (IsEntityVisible(cameraFrustum, effectiveModelMatrix, subMesh.aabbMin, subMesh.aabbMax)) {
                bool batchFound = false;

                // FIX: Never batch skeletal meshes. They require unique bone uniforms per entity.
                if (!isSkeletal) {
                    for (auto& batch : batches) {
                        if (batch.resource == res &&
                            batch.subMeshIndex == i &&
                            batch.materialId == cmd.Primitive.MaterialId &&
                            batch.boneMatrices.empty()) // Ensure we don't mix static into animated batches
                        {
                            batch.instanceMatrices.push_back(effectiveModelMatrix);
                            batchFound = true;
                            break;
                        }
                    }
                }

                if (!batchFound) {
                    RenderBatch newBatch;
                    newBatch.resource = res;
                    newBatch.subMeshIndex = i;
                    newBatch.materialId = cmd.Primitive.MaterialId;
                    newBatch.instanceMatrices.push_back(effectiveModelMatrix);

                    if (isSkeletal) {
                        newBatch.boneMatrices = cmd.Primitive.FinalBoneMatrices;

                        // Capture for Debug Drawing
                        auto skelComp = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(cmd.Primitive.Entity, "SkeletalMeshComponent"));
                        if (skelComp) mDebugSkeletonsToDraw.push_back(skelComp);
                    }
                    batches.push_back(newBatch);
                }
            }
        }
    }

    // Define Light for Shadows (Hardcoded for testing)
    glm::vec3 lightPos(-20.0f, 50.0f, -10.0f);
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

    for (auto& batch : batches) {
        if (batch.instanceMatrices.empty()) continue;

        // Upload Instance Data
        glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
        glBufferData(GL_ARRAY_BUFFER, batch.instanceMatrices.size() * sizeof(glm::mat4),
            batch.instanceMatrices.data(), GL_STREAM_DRAW);

        GLuint vao = batch.resource->VAOs[batch.subMeshIndex];
        uint32_t count = batch.resource->indexCounts[batch.subMeshIndex];

        // --- UPLOAD BONES FOR SHADOWS ---
        if (!batch.boneMatrices.empty()) {
            shadowMapShader->SetBool("uIsAnimated", true);

            // FIX: Pad bones to MAX_BONES (200) to prevent shader reading garbage
            std::vector<glm::mat4> safeBones = batch.boneMatrices;
            if (safeBones.size() < 200) safeBones.resize(200, glm::mat4(1.0f));

            glUniformMatrix4fv(
                glGetUniformLocation(shadowMapShader->get_program_id(), "finalBones"),
                200,
                GL_FALSE,
                glm::value_ptr(safeBones[0])
            );
        }
        else {
            shadowMapShader->SetBool("uIsAnimated", false);
        }

        glBindVertexArray(vao);
        SetupInstanceAttributes(vao); // Ensure instancing attributes are linked to this VAO
        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, (GLsizei)batch.instanceMatrices.size());
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ====================================================
    // PASS 2: Geometry Pass (G-Buffer)
    // ====================================================
    glViewport(0, 0, currentWidth, currentHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

    // Clear Color (Position/Normal/Albedo) AND Depth
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    geometryPassShader->Use();
    geometryPassShader->SetMat4("view", view);
    geometryPassShader->SetMat4("projection", projection);

    for (auto& batch : batches) {
        if (batch.instanceMatrices.empty()) continue;

        // 1. Upload Instancing Data
        glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
        glBufferData(GL_ARRAY_BUFFER, batch.instanceMatrices.size() * sizeof(glm::mat4),
            batch.instanceMatrices.data(), GL_STREAM_DRAW);

        CompiledMaterial* mat = assetManager_->GetMaterial(batch.materialId);
        Shader* currentShader = nullptr;

        // 2. Select Shader
        if (mat && mat->GLShader) {
            currentShader = mat->GLShader;
        }
        else {
            currentShader = geometryPassShader.get();
        }
        currentShader->Use();

        // 3. Set Common Uniforms
        currentShader->SetMat4("view", view);
        currentShader->SetMat4("projection", projection);

        // 4. Set Material Properties & Defaults
        bool hasTextures = false;

        if (mat) {
            // Apply Material Parameters (Colors, properties)
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
                    hasTextures = true;
                }
            }

            // Fallback defaults if material parameters are missing
            if (mat->m_Parameters.find("uAlbedo") == mat->m_Parameters.end())
                currentShader->SetVec3("uAlbedo", glm::vec3(1.0f, 1.0f, 1.0f));
            if (mat->m_Parameters.find("uRoughness") == mat->m_Parameters.end())
                currentShader->SetFloat("uRoughness", 0.5f);
            if (mat->m_Parameters.find("uMetallic") == mat->m_Parameters.end())
                currentShader->SetFloat("uMetallic", 0.0f);
        }
        else {
            // Default grey material for missing materials
            currentShader->SetVec3("uAlbedo", glm::vec3(0.8f, 0.8f, 0.8f));
            currentShader->SetFloat("uMetallic", 0.0f);
            currentShader->SetFloat("uRoughness", 0.5f);
        }

        // FIX: Explicitly set uUseTextures.
        // If false, shader uses uAlbedo. If true, it samples textures.
        currentShader->SetBool("uUseTextures", hasTextures);

        // --- UPLOAD BONES FOR GEOMETRY ---
        if (!batch.boneMatrices.empty()) {
            currentShader->SetBool("uIsAnimated", true);

            // FIX: Pad bones to MAX_BONES (200)
            std::vector<glm::mat4> safeBones = batch.boneMatrices;
            if (safeBones.size() < 200) safeBones.resize(200, glm::mat4(1.0f));

            GLint loc = glGetUniformLocation(currentShader->get_program_id(), "finalBones");
            if (loc != -1) {
                glUniformMatrix4fv(loc, 200, GL_FALSE, glm::value_ptr(safeBones[0]));
            }
        }
        else {
            currentShader->SetBool("uIsAnimated", false);
        }

        // FIX: BIND VAO! This was missing in your original code.
        GLuint vao = batch.resource->VAOs[batch.subMeshIndex];
        uint32_t count = batch.resource->indexCounts[batch.subMeshIndex];

        glBindVertexArray(vao);
        SetupInstanceAttributes(vao); // Re-bind attribute pointers for this VAO

        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, (GLsizei)batch.instanceMatrices.size());
    }

    // Clean up
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ====================================================
    // PASS 3: Lighting Pass (Quad Render)
    // ====================================================

    // Bind the final output framebuffer (usually 0 for screen, or your specific FBO)
    if (framebuffer) framebuffer->bind();
    else glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    lightingPassShader->Use();
    
    // Bind G-Buffer Textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    lightingPassShader->SetInt("gPosition", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    lightingPassShader->SetInt("gNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
    lightingPassShader->SetInt("gAlbedoSpec", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
    lightingPassShader->SetInt("shadowMap", 3);

    // Lighting Uniforms
    lightingPassShader->SetVec3("viewPos", camera->CameraTransform.position);
    lightingPassShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
    lightingPassShader->SetVec3("lightPos", lightPos);
    lightingPassShader->SetVec3("lightColor", glm::vec3(1.0, 0.95, 0.9));

    // Render Full Screen Quad
    // Note: We disable depth test because we want to draw over the whole screen
    // regardless of the previous depth buffer state.
    glDisable(GL_DEPTH_TEST);
    RenderQuad();
    glEnable(GL_DEPTH_TEST);

    // ====================================================
    // PASS 4: Debug Rendering (Skeletons)
    // ====================================================
    // Only draw debug on top of the lighting pass
    if (!mDebugSkeletonsToDraw.empty()) {
        glDisable(GL_DEPTH_TEST); // Draw on top
        RenderDebugSkeletons(mDebugSkeletonsToDraw, camera);
        glEnable(GL_DEPTH_TEST);
    }
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

static void BuildBoneQuad(
    const glm::vec3& p0,
    const glm::vec3& p1,
    const glm::mat4& view,
    float pixelThickness,
    float viewportHeight,
    std::vector<glm::vec3>& outVerts)
{
    // Transform to view space
    glm::vec4 v0 = view * glm::vec4(p0, 1.0f);
    glm::vec4 v1 = view * glm::vec4(p1, 1.0f);

    glm::vec3 dir = glm::normalize(glm::vec3(v1 - v0));

    // Camera looks down -Z in view space
    glm::vec3 viewDir(0.0f, 0.0f, -1.0f);

    glm::vec3 right = glm::normalize(glm::cross(dir, viewDir));
    if (glm::length(right) < 0.001f)
        right = glm::vec3(1, 0, 0);

    // Convert pixel thickness to view-space scale
    float halfThickness =
        (pixelThickness / viewportHeight) * -v0.z;

    glm::vec3 offset = right * halfThickness;

    glm::vec3 a = glm::vec3(v0) + offset;
    glm::vec3 b = glm::vec3(v0) - offset;
    glm::vec3 c = glm::vec3(v1) + offset;
    glm::vec3 d = glm::vec3(v1) - offset;

    // Two triangles
    outVerts.push_back(a);
    outVerts.push_back(b);
    outVerts.push_back(c);

    outVerts.push_back(c);
    outVerts.push_back(b);
    outVerts.push_back(d);
}
void RenderOpenGL::RenderDebugSkeletons(
    const std::vector<SkeletalMeshComponent*>& components,
    Camera* camera)
{
    if (components.empty()) return;

    glm::mat4 view = ReCamera::GetViewMatrix(*camera);
    glm::mat4 projection = ReCamera::GetProjectionMatrix(*camera);

    // Use your simple "LightSourceShader" (or any shader that outputs a solid color)
    // Assuming 'shader' is your simple solid-color shader
    shader->Use();
    shader->SetMat4("projection", projection);
    shader->SetMat4("view", view);
    shader->SetVec3("lightColor", glm::vec3(0.0f, 1.0f, 0.0f)); // Draw in Green
    shader->SetMat4("model", glm::mat4(1.0f));

    glBindVertexArray(mDebugLineVAO);

    mDebugQuadVertices.clear();

    constexpr float BONE_THICKNESS_PX = 4.0f;

    for (const auto* comp : components)
    {
        if (!comp || !comp->ShowDebugSkeleton) continue;

        const auto& lines = comp->DebugBoneLines;
        if (lines.size() < 6) continue;

        for (size_t i = 0; i + 5 < lines.size(); i += 6)
        {
            glm::vec3 p0(lines[i + 0], lines[i + 1], lines[i + 2]);
            glm::vec3 p1(lines[i + 3], lines[i + 4], lines[i + 5]);

            BuildBoneQuad(
                p0,
                p1,
                view,
                BONE_THICKNESS_PX,
                static_cast<float>(currentHeight),
                mDebugQuadVertices
            );
        }
    }

    if (mDebugQuadVertices.empty()) return;

    glBindVertexArray(mDebugLineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mDebugLineVBO);

    glBufferData(
        GL_ARRAY_BUFFER,
        mDebugQuadVertices.size() * sizeof(glm::vec3),
        mDebugQuadVertices.data(),
        GL_DYNAMIC_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0
    );

    glDrawArrays(GL_TRIANGLES, 0,
        static_cast<GLsizei>(mDebugQuadVertices.size()));

    glBindVertexArray(0);
    glUseProgram(0);
}


void RenderOpenGL::SetupInstanceAttributes(GLuint vao)
{
    glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
    GLsizei vec4Size = sizeof(glm::vec4);

    // CHANGE: Start at Attribute 10 instead of 5
    // 0=Pos, 1=Norm, 2=UV, 3=Tan, 4=BiTan, 5=BoneIDs, 6=Weights
    int startLoc = 10;

    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(startLoc + i);
        glVertexAttribPointer(startLoc + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));
        glVertexAttribDivisor(startLoc + i, 1);
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
