#pragma once
#include <vector>
#include <string>
#include "System/System.h"
#include "ReflectionMacros.h"

REFSYSTEM()
class REFLECTION_API WorldGenSystem : public System
{
public:
    REFVARIABLE()
        std::vector<std::string> ComponentsToRegister = {};

    void Update(float dt) override {}
    void Cleanup() override {}

    // Main function to call from your Game/Editor
    REFFUNCTION()
        void GenerateTerrain(int width, int depth, float scale, float heightMultiplier);

private:
    // Simple pseudo-random noise function
    float GetHeight(float x, float z, float scale, float heightMultiplier);

    // Helper to calculate normals for lighting
    glm::vec3 CalculateNormal(float x, float z, float scale, float heightMultiplier);
};

extern "C" REFLECTION_API System * CreateSystem();