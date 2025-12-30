// GPUScene.cpp
#include "GPUScene.h"

void GPUScene::Init() {
    cullShader = std::make_unique<Shader>("shaders/Computed/Cull.comp");

    // 1. Object Data Buffer (Big enough for MAX_OBJECTS)
    glGenBuffers(1, &ssboObjects);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboObjects);
    glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_OBJECTS * sizeof(ObjectData), nullptr, GL_DYNAMIC_DRAW);

    // 2. Indirect Draw Command Buffer
    glGenBuffers(1, &ssboIndirect);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboIndirect); // Bind as SSBO for writing
    glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_MESHES * sizeof(DrawElementsIndirectCommand), nullptr, GL_DYNAMIC_DRAW);
    // Bind as INDIRECT_BUFFER is done at draw time

    // 3. Instance Redirect Buffer
    glGenBuffers(1, &ssboInstances);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboInstances);
    glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_OBJECTS * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void GPUScene::SyncObjects(const std::vector<RenderPrimitive>& primitives, AssetManagerApi* assets) {
    currentObjectCount = (uint32_t)primitives.size();

    std::vector<ObjectData> gpuObjects;
    std::vector<DrawElementsIndirectCommand> gpuCommands;

    // We need to map Unique Mesh Resources to Command Indices
    // (In a real engine, you'd cache this map)
    std::vector<std::shared_ptr<MeshResource>> uniqueMeshes;

    gpuObjects.reserve(currentObjectCount);

    uint32_t globalInstanceAccumulator = 0;

    // 1. BUILD OBJECT LIST
    for (const auto& prim : primitives) {
        if (!prim.Mesh || !prim.Mesh->uploaded) continue;

        // Find or Add Mesh
        int meshIndex = -1;
        for (int i = 0; i < uniqueMeshes.size(); ++i) {
            if (uniqueMeshes[i] == prim.Mesh) { meshIndex = i; break; }
        }

        if (meshIndex == -1) {
            meshIndex = (int)uniqueMeshes.size();
            uniqueMeshes.push_back(prim.Mesh);

            // Create new command slot
            DrawElementsIndirectCommand cmd{};
            cmd.count = prim.Mesh->indexCounts[0]; // Assuming submesh 0 for simplicity
            cmd.instanceCount = 0; // Reset every frame (or via shader)
            cmd.firstIndex = 0; // Assuming 0 for simplicity (use prim.Mesh->EBO offset if needed)
            cmd.baseVertex = 0;
            cmd.baseInstance = globalInstanceAccumulator; // Where this mesh starts writing IDs

            // Reserve space in the redirect buffer for potentially ALL instances of this mesh
            // (Naive: Reserve MAX. Better: Calculate exact count)
            // For Level 1 simple: Just increment baseInstance by a safe amount
            // or perform a pre-pass to count instances per mesh.
            // Let's assume pre-pass done or simply:
            globalInstanceAccumulator += MAX_OBJECTS / MAX_MESHES; // Safe chunking

            gpuCommands.push_back(cmd);
        }

        ObjectData obj;
        obj.modelMatrix = prim.ModelMatrix;
        obj.aabbMin = glm::vec4(prim.Mesh->cpuMesh->aabbMin, 0.0f);
        obj.aabbMax = glm::vec4(prim.Mesh->cpuMesh->aabbMax, 0.0f);
        obj.meshIndex = meshIndex;
        gpuObjects.push_back(obj);
    }

    currentMeshCount = (uint32_t)gpuCommands.size();

    // 2. UPLOAD TO GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboObjects);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, gpuObjects.size() * sizeof(ObjectData), gpuObjects.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboIndirect);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, gpuCommands.size() * sizeof(DrawElementsIndirectCommand), gpuCommands.data());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void GPUScene::ExecuteCullShader(const glm::mat4& viewProj, const glm::vec3& camPos) {
    cullShader->Use();
    cullShader->SetMat4("viewProjection", viewProj);
    cullShader->SetInt("totalObjectCount", currentObjectCount);

    // Bind Buffers
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboObjects);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboIndirect);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboInstances);

    // Dispatch
    // Round up division: (N + 255) / 256
    glDispatchCompute((currentObjectCount + 255) / 256, 1, 1);

    // BARRIER: Wait for Compute to finish writing to Indirect Buffer
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
}

void GPUScene::DrawScene() {
    // Bind SSBOs so the Vertex Shader can read Object Data
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboObjects);       // Transforms
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboInstances);     // Visible IDs

    // Bind the Indirect Buffer (The GPU reads the "count" from here)
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ssboIndirect);

    // Loop through our unique meshes
    for (size_t i = 0; i < m_UniqueMeshes.size(); ++i) {
        auto& mesh = m_UniqueMeshes[i];
        if (!mesh) continue;

        // 1. Bind Geometry
        // Assuming sub-mesh 0 for this simplified manager. 
        // (For full multi-mesh support, your m_UniqueMeshes should store pair<Mesh*, subMeshIndex>)
        glBindVertexArray(mesh->VAOs[0]);

        // 2. Bind Material/Textures for this mesh
        // (You'll need to store the MaterialID in m_UniqueMeshes list too!)
        // compiledMaterial* mat = ...;
        // mat->Bind();

        // 3. INDIRECT DRAW
        // Instead of passing the count (CPU), we pass the OFFSET into the buffer (GPU).
        // The GPU reads memory at (i * sizeof(Command)) to find out how many instances to draw.
        // If the Compute Shader decided 0 instances are visible, this draws nothing (cheaply).
        glDrawElementsIndirect(GL_TRIANGLES,
            GL_UNSIGNED_INT,
            (void*)(i * sizeof(DrawElementsIndirectCommand)));
    }

    glBindVertexArray(0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

void GPUScene::ResetCounts() {
    // Bind the Indirect Buffer as a Shader Storage Buffer so we can write to it
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboIndirect);

    // We want to set 'instanceCount' to 0 for every command.
    // Since 'DrawElementsIndirectCommand' is:
    // { count, instanceCount, firstIndex, baseVertex, baseInstance }
    // We need to clear the 2nd integer of every struct.
    //
    // SIMPLE WAY: Clear the whole buffer to 0? 
    // NO! That deletes 'count' and 'firstIndex' which we need.

    // CORRECT WAY: Use a small Compute Shader or glClearBufferSubData loop.
    // FASTEST WAY (for Level 1): Just map it and clear the specific ints.

    DrawElementsIndirectCommand* cmds = (DrawElementsIndirectCommand*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
    if (cmds) {
        for (uint32_t i = 0; i < currentMeshCount; ++i) {
            cmds[i].instanceCount = 0;
        }
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
