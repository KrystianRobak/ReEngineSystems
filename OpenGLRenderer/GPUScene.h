// GPUScene.h
#pragma once
#include <GL/glew.h>
#include <vector>
#include "GpuTypes.h"
#include "AssetManager.h" // For MeshResource
#include "Shader.h"
#include "Commander.h"

class GPUScene {
public:
    void Init();
    void Cleanup();

    // Call this whenever the scene changes (entities added/moved)
    void SyncObjects(const std::vector<RenderPrimitive>& primitives, AssetManagerApi* assets);

    // Call this every frame before drawing
    void ExecuteCullShader(const glm::mat4& viewProj, const glm::vec3& camPos);

    // Call this to draw
    void DrawScene();

    void ResetCounts();

private:
    std::vector<std::shared_ptr<MeshResource>> m_UniqueMeshes;

    GLuint ssboObjects = 0;   // Binding 0
    GLuint ssboIndirect = 0;  // Binding 1 (Draw Commands)
    GLuint ssboInstances = 0; // Binding 2 (Visible IDs)

    std::unique_ptr<Shader> cullShader;

    uint32_t currentObjectCount = 0;
    uint32_t currentMeshCount = 0;
};