#include "Octree.h"
#include <algorithm>
#include <iostream>

Octree::Octree(const glm::vec3& center, float size) {
    root = std::make_unique<OctreeNode>(center, size);
}

bool Octree::OctreeNode::intersects(const AABB& aabb) const {
    glm::vec3 nodeMin = center - glm::vec3(size / 2);
    glm::vec3 nodeMax = center + glm::vec3(size / 2);

    return !(aabb.GetMax().x < nodeMin.x || aabb.GetMin().x > nodeMax.x ||
        aabb.GetMax().y < nodeMin.y || aabb.GetMin().y > nodeMax.y ||
        aabb.GetMax().z < nodeMin.z || aabb.GetMin().z > nodeMax.z);
}

int Octree::GetOctant(const glm::vec3& nodeCenter, const glm::vec3& point) {
    int octant = 0;
    if (point.x >= nodeCenter.x) octant |= 1;
    if (point.y >= nodeCenter.y) octant |= 2;
    if (point.z >= nodeCenter.z) octant |= 4;
    return octant;
}

bool Octree::CheckAABBCollision(const AABB& a, const AABB& b) {
    return !(a.GetMin().x > b.GetMax().x || a.GetMax().x < b.GetMin().x ||
        a.GetMin().y > b.GetMax().y || a.GetMax().y < b.GetMin().y ||
        a.GetMin().z > b.GetMax().z || a.GetMax().z < b.GetMin().z);
}

void Octree::BuildStatic(const std::vector<AABB*>& statics) {
    // We assume the root bounds are already set large enough 
    // (You can expand this to auto-resize root if needed, but keeping it simple is faster)
    for (AABB* obj : statics) {
        InsertStatic(root.get(), obj, 0);
    }
}

void Octree::InsertStatic(OctreeNode* node, AABB* obj, int depth) {
    if (!node->intersects(*obj)) return;

    // Split if needed
    if (!node->hasChildren && depth < MAX_DEPTH && node->staticObjects.size() >= MAX_OBJECTS) {
        float childSize = node->size / 2.0f;
        float offset = childSize / 2.0f;
        for (int i = 0; i < 8; i++) {
            glm::vec3 newCenter = node->center;
            newCenter.x += (i & 1) ? offset : -offset;
            newCenter.y += (i & 2) ? offset : -offset;
            newCenter.z += (i & 4) ? offset : -offset;
            node->children[i] = std::make_unique<OctreeNode>(newCenter, childSize);
        }
        node->hasChildren = true;

        // Re-distribute existing static objects to children if possible
        // (Optional optimization: moves objects down the tree)
        std::vector<AABB*> keep;
        for (auto* existing : node->staticObjects) {
            // Logic to push down would go here, similar to below
            keep.push_back(existing);
        }
        node->staticObjects = keep;
    }

    if (node->hasChildren) {
        // Try to fit into a child
        glm::vec3 objCenter = (obj->GetMin() + obj->GetMax()) * 0.5f;
        int octant = GetOctant(node->center, objCenter);

        // If it fits entirely in the child octant
        if (node->children[octant]->intersects(*obj)) {
            InsertStatic(node->children[octant].get(), obj, depth + 1);
            return;
        }
    }

    // Otherwise, keep it here
    node->staticObjects.push_back(obj);
}

void Octree::Update(const std::vector<AABB*>& dynamics, std::vector<std::pair<AABB*, AABB*>>& outCollisions) {
    // 1. Clear old dynamic data
    ClearDynamicRecursive(root.get());

    // 2. Insert new dynamic objects
    for (AABB* obj : dynamics) {
        InsertDynamic(root.get(), obj, 0);
    }

    // 3. Check collisions
    // We pass empty ancestor lists to start
    std::vector<AABB*> staticAncestors;
    std::vector<AABB*> dynamicAncestors;
    CheckNodeCollisions(root.get(), staticAncestors, dynamicAncestors, outCollisions);
}

void Octree::ClearDynamicRecursive(OctreeNode* node) {
    node->dynamicObjects.clear();
    if (node->hasChildren) {
        for (auto& child : node->children) {
            ClearDynamicRecursive(child.get());
        }
    }
}

void Octree::InsertDynamic(OctreeNode* node, AABB* obj, int depth) {
    if (!node->intersects(*obj)) return;

    // Note: We do NOT subdivide based on dynamic objects alone to keep the tree stable.
    // The static geometry usually dictates the density of the world.
    // However, if you have an empty world with lots of physics, you might want to enable subdivision here too.

    if (node->hasChildren) {
        glm::vec3 objCenter = (obj->GetMin() + obj->GetMax()) * 0.5f;
        int octant = GetOctant(node->center, objCenter);

        if (node->children[octant]->intersects(*obj)) {
            InsertDynamic(node->children[octant].get(), obj, depth + 1);
            return;
        }
    }

    node->dynamicObjects.push_back(obj);
}

void Octree::CheckNodeCollisions(OctreeNode* node,
    const std::vector<AABB*>& staticAncestors,
    const std::vector<AABB*>& dynamicAncestors,
    std::vector<std::pair<AABB*, AABB*>>& collisions) {

    // 1. Check Dynamic vs Dynamic (Local)
    for (size_t i = 0; i < node->dynamicObjects.size(); i++) {
        for (size_t j = i + 1; j < node->dynamicObjects.size(); j++) {
            if (CheckAABBCollision(*node->dynamicObjects[i], *node->dynamicObjects[j])) {
                node->dynamicObjects[i]->SetIsColliding(true);
                node->dynamicObjects[j]->SetIsColliding(true);
                collisions.emplace_back(node->dynamicObjects[i], node->dynamicObjects[j]);
            }
        }
    }

    // 2. Check Dynamic vs Static (Local)
    for (auto* dyn : node->dynamicObjects) {
        for (auto* stat : node->staticObjects) {
            if (CheckAABBCollision(*dyn, *stat)) {
                dyn->SetIsColliding(true);
                stat->SetIsColliding(true);
                collisions.emplace_back(dyn, stat);
            }
        }
    }

    // 3. Check Dynamic vs Ancestors
    for (auto* dyn : node->dynamicObjects) {
        // Dynamic vs Static Ancestors
        for (auto* statAnc : staticAncestors) {
            if (CheckAABBCollision(*dyn, *statAnc)) {
                dyn->SetIsColliding(true);
                statAnc->SetIsColliding(true);
                collisions.emplace_back(dyn, statAnc);
            }
        }
        // Dynamic vs Dynamic Ancestors
        for (auto* dynAnc : dynamicAncestors) {
            if (CheckAABBCollision(*dyn, *dynAnc)) {
                dyn->SetIsColliding(true);
                dynAnc->SetIsColliding(true);
                collisions.emplace_back(dyn, dynAnc);
            }
        }
    }

    // 4. Check Static (Local) vs Dynamic Ancestors
    // (We skip Static vs Static)
    for (auto* stat : node->staticObjects) {
        for (auto* dynAnc : dynamicAncestors) {
            if (CheckAABBCollision(*stat, *dynAnc)) {
                stat->SetIsColliding(true);
                dynAnc->SetIsColliding(true);
                collisions.emplace_back(stat, dynAnc);
            }
        }
    }

    // 5. Recurse to children
    if (node->hasChildren) {
        // Create new ancestor lists for children (copying is cheap for pointers)
        std::vector<AABB*> nextStaticAncestors = staticAncestors;
        nextStaticAncestors.insert(nextStaticAncestors.end(), node->staticObjects.begin(), node->staticObjects.end());

        std::vector<AABB*> nextDynamicAncestors = dynamicAncestors;
        nextDynamicAncestors.insert(nextDynamicAncestors.end(), node->dynamicObjects.begin(), node->dynamicObjects.end());

        for (auto& child : node->children) {
            CheckNodeCollisions(child.get(), nextStaticAncestors, nextDynamicAncestors, collisions);
        }
    }
}