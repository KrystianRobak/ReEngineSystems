#include "RenderOpenGL.h"

#include "Components/Transform.h"
#include "Components/LightSource.h"
#include <thread>
#include "MeshData.h"
#include "StaticMesh.h"
#include "StaticMeshData.h"
#include "MaterialSystem/Material.h"

#include "ReScene.h"
#include <gtc/type_ptr.hpp>

struct GPULight {
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 color;
    float intensity;
    int type;
    // Attenuation
    float constant;
    float linear;
    float quadratic;
    // Spot
    float cutOff;
    float outerCutOff;
};

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

void RenderOpenGL::InitApi(Editor::IEngineEditorApi* engine, IApplicationApi* application, std::shared_ptr<AssetManagerApi> AssetManger)
{
    engine_ = engine;
	application_ = application;

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

    InitSkybox();

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
    // --- 0. PREPARATION ---
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

            if (IsEntityVisible(cameraFrustum, effectiveModelMatrix, subMesh.aabbMin, subMesh.aabbMax)) {
                bool batchFound = false;

                if (!isSkeletal) {
                    for (auto& batch : batches) {
                        if (batch.resource == res &&
                            batch.subMeshIndex == i &&
                            batch.materialId == cmd.Primitive.MaterialId &&
                            batch.boneMatrices.empty())
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

                        auto skelComp = static_cast<SkeletalMeshComponent*>(engine_->GetComponent(cmd.Primitive.Entity, "SkeletalMeshComponent"));
                        if (skelComp) mDebugSkeletonsToDraw.push_back(skelComp);
                    }
                    batches.push_back(newBatch);
                }
            }
        }
    }

    // --- 2. LIGHT & SHADOW SETUP ---
    std::vector<GPULight> gpuLights;
    glm::mat4 shadowLightSpaceMatrix = glm::mat4(1.0f);
    bool shadowCasterFound = false;

    auto& entities = mEntities;
    for (auto& entity : entities) {
        if (engine_->HasComponent(entity, "LightSource") && engine_->HasComponent(entity, "Transform")) {

            auto lightComp = (LightSource*)engine_->GetComponent(entity, "LightSource");
            auto transformComp = (Transform*)engine_->GetComponent(entity, "Transform");

            GPULight l;
            l.type = lightComp->type;
            l.color = lightComp->LightColor;
            l.intensity = lightComp->intensity;

            // Get World Position and Direction from Transform Matrix
            const glm::mat4& model = ReCamera::GetModelMatrix(*transformComp);
            l.position = glm::vec3(model[3]);
            l.direction = glm::normalize(-glm::vec3(model[2])); // Forward vector

            l.constant = lightComp->constant;
            l.linear = lightComp->linear;
            l.quadratic = lightComp->quadratic;
            l.cutOff = lightComp->cutOff;
            l.outerCutOff = lightComp->outerCutOff;

            gpuLights.push_back(l);

            // Logic to pick the Shadow Caster (First Directional Light wins)
            if (!shadowCasterFound && l.type == (int)LightType::Directional) {
                // FIX 1: Increased Ortho size to ensure the scene fits in the shadow map
                float near_plane = 1.0f, far_plane = 200.0f;
                glm::mat4 lightProjection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, near_plane, far_plane);

                glm::vec3 target = l.position + l.direction;

                // FIX 2: Prevent LookAt Crash (NaN) when looking straight Up/Down
                glm::vec3 upVector = glm::vec3(0, 1, 0);
                if (glm::abs(l.direction.y) > 0.99f) {
                    upVector = glm::vec3(1, 0, 0);
                }

                glm::mat4 lightView = glm::lookAt(l.position, target, upVector);

                shadowLightSpaceMatrix = lightProjection * lightView;
                shadowCasterFound = true;
            }
        }
    }

    // Sort lights (Directional first)
    std::sort(gpuLights.begin(), gpuLights.end(), [](const GPULight& a, const GPULight& b) {
        return a.type < b.type;
        });

    // Fallback if no light is found
    if (!shadowCasterFound) {
        glm::mat4 lightProjection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, 1.0f, 200.0f);
        glm::mat4 lightView = glm::lookAt(glm::vec3(-20.0f, 50.0f, -10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        shadowLightSpaceMatrix = lightProjection * lightView;
    }

    // ====================================================
    // PASS 1: Shadow Map (Depth Only)
    // ====================================================
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO);
    glClear(GL_DEPTH_BUFFER_BIT);

    // FIX 3: Front Face Culling to solve Peter Panning (Detached Shadows)
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    shadowMapShader->Use();
    shadowMapShader->SetMat4("lightSpaceMatrix", shadowLightSpaceMatrix);

    for (auto& batch : batches) {
        if (batch.instanceMatrices.empty()) continue;

        glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
        glBufferData(GL_ARRAY_BUFFER, batch.instanceMatrices.size() * sizeof(glm::mat4),
            batch.instanceMatrices.data(), GL_STREAM_DRAW);

        GLuint vao = batch.resource->VAOs[batch.subMeshIndex];
        uint32_t count = batch.resource->indexCounts[batch.subMeshIndex];

        if (!batch.boneMatrices.empty()) {
            shadowMapShader->SetBool("uIsAnimated", true);

            // Safe bone upload
            std::vector<glm::mat4> safeBones = batch.boneMatrices;
            if (safeBones.size() < 200) safeBones.resize(200, glm::mat4(1.0f));

            glUniformMatrix4fv(
                glGetUniformLocation(shadowMapShader->get_program_id(), "finalBones"),
                200, GL_FALSE, glm::value_ptr(safeBones[0])
            );
        }
        else {
            shadowMapShader->SetBool("uIsAnimated", false);
        }

        glBindVertexArray(vao);
        SetupInstanceAttributes(vao);
        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, (GLsizei)batch.instanceMatrices.size());
    }

    // Reset Culling for Geometry Pass
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ====================================================
    // PASS 2: Geometry Pass (G-Buffer)
    // ====================================================
    glViewport(0, 0, currentWidth, currentHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    geometryPassShader->Use();
    geometryPassShader->SetMat4("view", view);
    geometryPassShader->SetMat4("projection", projection);

    for (auto& batch : batches) {
        if (batch.instanceMatrices.empty()) continue;

        glBindBuffer(GL_ARRAY_BUFFER, mInstanceVBO);
        glBufferData(GL_ARRAY_BUFFER, batch.instanceMatrices.size() * sizeof(glm::mat4),
            batch.instanceMatrices.data(), GL_STREAM_DRAW);

        CompiledMaterial* mat = assetManager_->GetMaterial(batch.materialId);
        Shader* currentShader = nullptr;

        if (mat && mat->GLShader) {
            currentShader = mat->GLShader.get();
        }
        else {
            currentShader = geometryPassShader.get();
        }
        currentShader->Use();

        currentShader->SetMat4("view", view);
        currentShader->SetMat4("projection", projection);

        bool hasTextures = false;

        if (mat) {
            for (auto& [name, value] : mat->m_Parameters) {
                currentShader->SetVec4(name, value);
            }

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

            // Set defaults if not present
            if (mat->m_Parameters.find("uAlbedo") == mat->m_Parameters.end())
                currentShader->SetVec3("uAlbedo", glm::vec3(1.0f, 1.0f, 1.0f));
            if (mat->m_Parameters.find("uRoughness") == mat->m_Parameters.end())
                currentShader->SetFloat("uRoughness", 0.5f);
            if (mat->m_Parameters.find("uMetallic") == mat->m_Parameters.end())
                currentShader->SetFloat("uMetallic", 0.0f);
        }
        else {
            currentShader->SetVec3("uAlbedo", glm::vec3(0.8f, 0.8f, 0.8f));
            currentShader->SetFloat("uMetallic", 0.0f);
            currentShader->SetFloat("uRoughness", 0.5f);
        }

        currentShader->SetBool("uUseTextures", hasTextures);

        if (!batch.boneMatrices.empty()) {
            currentShader->SetBool("uIsAnimated", true);
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

        GLuint vao = batch.resource->VAOs[batch.subMeshIndex];
        uint32_t count = batch.resource->indexCounts[batch.subMeshIndex];

        glBindVertexArray(vao);
        SetupInstanceAttributes(vao);
        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0, (GLsizei)batch.instanceMatrices.size());
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ====================================================
    // PASS 3: Lighting Pass (Quad Render)
    // ====================================================

    if (framebuffer) framebuffer->bind();
    else glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    lightingPassShader->Use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    lightingPassShader->SetInt("gPosition", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    lightingPassShader->SetInt("gNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
    lightingPassShader->SetInt("gAlbedoSpec", 2);

    // Bind Shadow Map to Slot 3
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
    lightingPassShader->SetInt("shadowMap", 3);
    lightingPassShader->SetVec3("viewPos", camera->CameraTransform.position);
    lightingPassShader->SetMat4("lightSpaceMatrix", shadowLightSpaceMatrix);

    lightingPassShader->SetInt("uLightCount", (int)gpuLights.size());

    for (size_t i = 0; i < gpuLights.size(); ++i) {
        if (i >= 32) break;

        std::string base = "lights[" + std::to_string(i) + "]";

        lightingPassShader->SetInt(base + ".type", gpuLights[i].type);
        lightingPassShader->SetVec3(base + ".Position", gpuLights[i].position);
        lightingPassShader->SetVec3(base + ".Direction", gpuLights[i].direction);
        lightingPassShader->SetVec3(base + ".Color", gpuLights[i].color);
        lightingPassShader->SetFloat(base + ".Intensity", gpuLights[i].intensity);

        lightingPassShader->SetFloat(base + ".Constant", gpuLights[i].constant);
        lightingPassShader->SetFloat(base + ".Linear", gpuLights[i].linear);
        lightingPassShader->SetFloat(base + ".Quadratic", gpuLights[i].quadratic);
        lightingPassShader->SetFloat(base + ".CutOff", gpuLights[i].cutOff);
        lightingPassShader->SetFloat(base + ".OuterCutOff", gpuLights[i].outerCutOff);
    }

    glDisable(GL_DEPTH_TEST);
    RenderQuad();
    glEnable(GL_DEPTH_TEST);

    // ====================================================
    // PASS 4: Skybox Pass
    // ====================================================

    GLuint targetFBO = framebuffer ? framebuffer->GetFBO() : 0;

    // Blit Depth Buffer so Skybox renders behind objects
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO);
    glBlitFramebuffer(0, 0, currentWidth, currentHeight,
        0, 0, currentWidth, currentHeight,
        GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);

    glDepthFunc(GL_LEQUAL);
    skyboxShader->Use();

    glm::mat4 viewNoTrans = glm::mat4(glm::mat3(view));
    skyboxShader->SetMat4("view", viewNoTrans);
    skyboxShader->SetMat4("projection", projection);

    glBindVertexArray(skyboxVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture);
    skyboxShader->SetInt("skybox", 0);

    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthFunc(GL_LESS);

    // ====================================================
    // PASS 5: Debug Rendering
    // ====================================================
    RenderDebugSkeletons(mDebugSkeletonsToDraw, camera);
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
    glm::vec4 v0 = view * glm::vec4(p0, 1.0f);
    glm::vec4 v1 = view * glm::vec4(p1, 1.0f);

    glm::vec3 dir = glm::normalize(glm::vec3(v1 - v0));

    glm::vec3 viewDir(0.0f, 0.0f, -1.0f);

    glm::vec3 right = glm::normalize(glm::cross(dir, viewDir));
    if (glm::length(right) < 0.001f)
        right = glm::vec3(1, 0, 0);

    float halfThickness =
        (pixelThickness / viewportHeight) * -v0.z;

    glm::vec3 offset = right * halfThickness;

    glm::vec3 a = glm::vec3(v0) + offset;
    glm::vec3 b = glm::vec3(v0) - offset;
    glm::vec3 c = glm::vec3(v1) + offset;
    glm::vec3 d = glm::vec3(v1) - offset;

    outVerts.push_back(a);
    outVerts.push_back(b);
    outVerts.push_back(c);

    outVerts.push_back(c);
    outVerts.push_back(b);
    outVerts.push_back(d);
}

void RenderOpenGL::InitSkybox()
{
    skyboxShader = std::make_unique<Shader>("shaders/Skybox/Skybox.vs", "shaders/Skybox/Skybox.fs");

    float skyboxVertices[] = {
        // positions          
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };

    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Order: Right, Left, Top, Bottom, Front, Back
    // You need 6 images named appropriately in your assets folder
    std::vector<std::string> faces
    {
        "shaders/Skybox/right.jpg",
        "shaders/Skybox/left.jpg",
        "shaders/Skybox/top.jpg",
        "shaders/Skybox/bottom.jpg",
        "shaders/Skybox/front.jpg",
        "shaders/Skybox/back.jpg"
    };

    skyboxTexture = LoadCubemap(faces);
}

unsigned int RenderOpenGL::LoadCubemap(std::vector<std::string> faces)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++)
    {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data
            );
            stbi_image_free(data);
        }
        else
        {
            LOGF_ERROR("Cubemap texture failed to load at path: %s", faces[i].c_str());
            stbi_image_free(data);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

void RenderOpenGL::RenderDebugSkeletons(
    const std::vector<SkeletalMeshComponent*>& components,
    Camera* camera)
{
    if (components.empty()) return;

    glm::mat4 view = ReCamera::GetViewMatrix(*camera);
    glm::mat4 projection = ReCamera::GetProjectionMatrix(*camera);

    shader->Use();
    shader->SetMat4("projection", projection);
    shader->SetMat4("view", view);
    shader->SetVec3("lightColor", glm::vec3(0.0f, 1.0f, 0.0f));
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

    glGenTextures(1, &gPosition);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition, 0);

    glGenTextures(1, &gNormal);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);

    glGenTextures(1, &gAlbedoSpec);
    glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoSpec, 0);

    unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, attachments);

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

    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

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
