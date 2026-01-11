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
    Entity terrain = engine_->CreateEntity();

    // Always use GetComponentForWrite for initial setup to ensure data is 
    // propagated from Write Buffer to Read Buffer during the next SwapData()
    engine_->AddComponent(terrain, "Transform");
    Transform* t = static_cast<Transform*>(engine_->GetComponentForWrite(terrain, "Transform"));
    t->position = glm::vec3(0, -5, 0);
    t->scale = glm::vec3(1, 1, 1);
    engine_->MarkEntityDirty(terrain, "Transform");

    // 2. Setup the HeightfieldCollider (The source of truth for heights)
    engine_->AddComponent(terrain, "HeightfieldCollider");
    HeightfieldCollider* hf = static_cast<HeightfieldCollider*>(engine_->GetComponentForWrite(terrain, "HeightfieldCollider"));
    hf->width = width;
    hf->depth = depth;
    hf->scale = scale;
    hf->heightMultiplier = heightMultiplier;
    hf->halfWidth = (float)width * 0.5f;
    hf->halfDepth = (float)depth * 0.5f;

    // Initialize its AABB for the Octree
    hf->Update(t->position);

    MeshData meshData;

    for (int z = 0; z < depth; z++) {
        for (int x = 0; x < width; x++) {
            float posX = x - hf->halfWidth;
            float posZ = z - hf->halfDepth;

            // Sample height from the collider logic to ensure physical alignment
            float posY = hf->GetHeightAt(posX, posZ);

            meshData.vertices.push_back(glm::vec3(posX, posY, posZ));

            // Finite difference for Normals
            float hL = hf->GetHeightAt(posX - 0.1f, posZ);
            float hR = hf->GetHeightAt(posX + 0.1f, posZ);
            float hD = hf->GetHeightAt(posX, posZ - 0.1f);
            float hU = hf->GetHeightAt(posX, posZ + 0.1f);
            meshData.Normals.push_back(glm::normalize(glm::vec3(hL - hR, 0.2f, hD - hU)));

            meshData.TexCoords.push_back(glm::vec2((float)x / width, (float)z / depth));
        }
    }

    // 4. Generate Indices
    for (int z = 0; z < depth - 1; z++) {
        for (int x = 0; x < width - 1; x++) {
            int topLeft = (z * width) + x;
            int topRight = topLeft + 1;
            int bottomLeft = ((z + 1) * width) + x;
            int bottomRight = bottomLeft + 1;

            meshData.indices.push_back(topLeft);
            meshData.indices.push_back(bottomLeft);
            meshData.indices.push_back(topRight);

            meshData.indices.push_back(topRight);
            meshData.indices.push_back(bottomLeft);
            meshData.indices.push_back(bottomRight);
        }
    }

    staticMeshData->meshes.push_back(meshData);
    staticMeshData->path = "Generated_Terrain";

    // 5. Finalize Components
    engine_->AddComponent(terrain, "StaticMesh");
    std::shared_ptr<MeshResource> gpuHandle = assetManager_->CreateManualMesh("Terrain_Gen_1", staticMeshData);

    // Assign to your component
    StaticMesh* meshComp = static_cast<StaticMesh*>(engine_->GetComponentForWrite(terrain, "StaticMesh"));
    meshComp->MeshResource = gpuHandle; // The component now holds the handle that will eventually be 'uploaded'

    engine_->AddComponent(terrain, "RigidBody");
    RigidBody* rb = static_cast<RigidBody*>(engine_->GetComponentForWrite(terrain, "RigidBody"));
    rb->isStatic = true;
    rb->mass = 0.0f; // Static objects typically have 0 mass (infinite)
}

extern "C" {
    REFLECTION_API System* CreateSystem() {
        return new WorldGenSystem();
    }
}