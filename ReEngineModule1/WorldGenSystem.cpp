#include "WorldGenSystem.h"
#include "Components/Transform.h"
#include "Components/RigidBody.h"
#include "Components/BoxCollider.h"
#include "Components/StaticMesh.h"
#include "AssetManager.h"
#include "StaticMeshData.h"
#include "Components/HeightfieldCollider.h"
#include <cmath>

// Simple pseudo-random height generation
float WorldGenSystem::GetHeight(float x, float z, float scale, float heightMultiplier) {
    // Combine sine waves for a "hilly" look without needing an external noise library
    float y = std::sin(x * scale) * std::cos(z * scale);
    y += std::sin(x * scale * 2.5f) * std::cos(z * scale * 2.5f) * 0.5f; // Detail layer
    return y * heightMultiplier;
}

glm::vec3 WorldGenSystem::CalculateNormal(float x, float z, float scale, float heightMultiplier) {
    // Calculate finite difference to find the slope
    float hL = GetHeight(x - 1.0f, z, scale, heightMultiplier);
    float hR = GetHeight(x + 1.0f, z, scale, heightMultiplier);
    float hD = GetHeight(x, z - 1.0f, scale, heightMultiplier);
    float hU = GetHeight(x, z + 1.0f, scale, heightMultiplier);

    // Normal vector is perpendicular to the slope
    glm::vec3 normal(hL - hR, 2.0f, hD - hU);
    return glm::normalize(normal);
}

void WorldGenSystem::GenerateTerrain(int width, int depth, float scale, float heightMultiplier) {

    auto staticMeshData = std::make_shared<StaticMeshData>();
    MeshData meshData;

    // 1. Generate Vertices
    float halfW = width * 0.5f;
    float halfD = depth * 0.5f;

    for (int z = 0; z < depth; z++) {
        for (int x = 0; x < width; x++) {
            Vertex v;

            // Center the terrain around 0,0
            float posX = x - halfW;
            float posZ = z - halfD;
            float posY = GetHeight(posX, posZ, scale, heightMultiplier);

            v.Position = glm::vec3(posX, posY, posZ);
            v.Normal = CalculateNormal(posX, posZ, scale, heightMultiplier);
            v.TexCoords = glm::vec2((float)x / width, (float)z / depth); // UVs 0-1

            // Tangents (simplified)
            v.Tangent = glm::vec3(1, 0, 0);
            v.Bitangent = glm::vec3(0, 0, 1);

            meshData.vertices.push_back(v);
        }
    }

    // 2. Generate Indices (Triangles)
    for (int z = 0; z < depth - 1; z++) {
        for (int x = 0; x < width - 1; x++) {
            int topLeft = (z * width) + x;
            int topRight = topLeft + 1;
            int bottomLeft = ((z + 1) * width) + x;
            int bottomRight = bottomLeft + 1;

            // Triangle 1
            meshData.indices.push_back(topLeft);
            meshData.indices.push_back(bottomLeft);
            meshData.indices.push_back(topRight);

            // Triangle 2
            meshData.indices.push_back(topRight);
            meshData.indices.push_back(bottomLeft);
            meshData.indices.push_back(bottomRight);
        }
    }

    // 3. Store Data and Register
    staticMeshData->meshes.push_back(meshData);

    // Calculate AABB for the mesh
    glm::vec3 min(FLT_MAX);
    glm::vec3 max(-FLT_MAX);
    for (auto& v : meshData.vertices) {
        min = glm::min(min, v.Position);
        max = glm::max(max, v.Position);
    }
    staticMeshData->aabbMin = min;
    staticMeshData->aabbMax = max;
    staticMeshData->path = "Generated_Terrain"; // Virtual path

    // 4. Create Entity in Scene
    Entity terrain = engine_->CreateEntity();
	engine_->AddComponent(terrain, "Transform");

    // Add Transform
    Transform* t = static_cast<Transform*>(engine_->GetComponent(terrain, "Transform"));
    t->position = glm::vec3(0, -5, 0); // Move down slightly
    t->scale = glm::vec3(1, 1, 1);

    // Add Mesh Component
    // Assuming you have a StaticMeshComponent that takes an ID
    engine_->AddComponent(terrain, "StaticMesh");
    StaticMesh* meshComp = static_cast<StaticMesh*>(engine_->GetComponent(terrain, "StaticMesh"));
    meshComp->MeshResource;
    // meshComp->materialId = ... (Assign a default material here)

    // Add Physics (Floor)
    engine_->AddComponent(terrain, "RigidBody");
    RigidBody* rb = static_cast<RigidBody*>(engine_->GetComponent(terrain, "RigidBody"));
    rb->isStatic = true; // IMPORTANT: The floor must be static

    // 5. Add Optimized Heightfield Collider (REPLACEMENT FOR BOXCOLLIDER)
    engine_->AddComponent(terrain, "HeightfieldCollider");
    HeightfieldCollider* hf = static_cast<HeightfieldCollider*>(engine_->GetComponent(terrain, "HeightfieldCollider"));
    hf->width = width;
    hf->depth = depth;
    hf->scale = scale;
    hf->heightMultiplier = heightMultiplier;

    // Store half-extents to quickly check if object is within terrain XZ bounds
    hf->halfWidth = (float)width * 0.5f;
    hf->halfDepth = (float)depth * 0.5f;
}

extern "C" {
    REFLECTION_API System* CreateSystem()
    {
        return new WorldGenSystem();
    }
}