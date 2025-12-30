#pragma once

#include <glm/glm.hpp>

// Max limits (adjust as needed)
#define MAX_OBJECTS 100000
#define MAX_MESHES 1024

struct DrawElementsIndirectCommand {
    uint32_t count;         // Num indices (from Mesh)
    uint32_t instanceCount; // Calculated by Compute Shader
    uint32_t firstIndex;    // Index offset (from Mesh)
    uint32_t baseVertex;    // Vertex offset (from Mesh)
    uint32_t baseInstance;  // Offset into the InstanceID buffer
};

// Represents one object in the world
struct ObjectData {
    glm::mat4 modelMatrix;
    glm::vec4 aabbMin; // .w is padding
    glm::vec4 aabbMax; // .w is padding
    uint32_t meshIndex;     // Which DrawCommand does this belong to?
    uint32_t padding[3];    // Align to 16 bytes (std430 alignment)
};