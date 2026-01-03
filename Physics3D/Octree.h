#pragma once

#include <vector>
#include <array>
#include <memory>
#include <glm/vec3.hpp>
#include "Collision/AABB.h"

class Octree {
private:
    struct OctreeNode {
        glm::vec3 center;
        float size;

        // Separate lists to avoid rebuilding static parts
        std::vector<AABB*> staticObjects;
        std::vector<AABB*> dynamicObjects;

        std::array<std::unique_ptr<OctreeNode>, 8> children;
        bool hasChildren = false;

        OctreeNode(const glm::vec3& c, float s) : center(c), size(s) {}

        // Check if an AABB intersects this node's bounds
        bool intersects(const AABB& aabb) const;
    };

    std::unique_ptr<OctreeNode> root;
    float minNodeSize = 1.0f; // Prevent infinite recursion on small overlapping objects
    static constexpr int MAX_OBJECTS = 8;
    static constexpr int MAX_DEPTH = 8;

    // Recursive helpers
    void InsertStatic(OctreeNode* node, AABB* obj, int depth);
    void InsertDynamic(OctreeNode* node, AABB* obj, int depth);
    void ClearDynamicRecursive(OctreeNode* node);

    // The core optimized collision logic
    void CheckNodeCollisions(OctreeNode* node,
        const std::vector<AABB*>& staticAncestors,
        const std::vector<AABB*>& dynamicAncestors,
        std::vector<std::pair<AABB*, AABB*>>& collisions);

    // Helper math
    int GetOctant(const glm::vec3& nodeCenter, const glm::vec3& point);
    bool CheckAABBCollision(const AABB& a, const AABB& b);

public:
    Octree(const glm::vec3& center, float size);

    // Call this ONCE (or when static geometry changes)
    void BuildStatic(const std::vector<AABB*>& statics);

    // Call this EVERY FRAME
    // It clears only dynamic data, re-inserts, and checks collisions
    void Update(const std::vector<AABB*>& dynamics, std::vector<std::pair<AABB*, AABB*>>& outCollisions);
};