#pragma once
#include <glm/glm.hpp>
#include <array>

struct Plane {
    glm::vec3 normal;
    float distance;

    // Normalize the plane equation
    void normalize() {
        float length = glm::length(normal);
        normal /= length;
        distance /= length;
    }
};

class Frustum {
public:
    std::array<Plane, 6> planes;

    // Extract planes from View-Projection Matrix
    void Update(const glm::mat4& viewProjection) {
        const float* m = (const float*)&viewProjection;

        // Left
        planes[0].normal.x = m[3] + m[0];
        planes[0].normal.y = m[7] + m[4];
        planes[0].normal.z = m[11] + m[8];
        planes[0].distance = m[15] + m[12];

        // Right
        planes[1].normal.x = m[3] - m[0];
        planes[1].normal.y = m[7] - m[4];
        planes[1].normal.z = m[11] - m[8];
        planes[1].distance = m[15] - m[12];

        // Bottom
        planes[2].normal.x = m[3] + m[1];
        planes[2].normal.y = m[7] + m[5];
        planes[2].normal.z = m[11] + m[9];
        planes[2].distance = m[15] + m[13];

        // Top
        planes[3].normal.x = m[3] - m[1];
        planes[3].normal.y = m[7] - m[5];
        planes[3].normal.z = m[11] - m[9];
        planes[3].distance = m[15] - m[13];

        // Near
        planes[4].normal.x = m[3] + m[2];
        planes[4].normal.y = m[7] + m[6];
        planes[4].normal.z = m[11] + m[10];
        planes[4].distance = m[15] + m[14];

        // Far
        planes[5].normal.x = m[3] - m[2];
        planes[5].normal.y = m[7] - m[6];
        planes[5].normal.z = m[11] - m[10];
        planes[5].distance = m[15] - m[14];

        for (auto& plane : planes) plane.normalize();
    }

    // Check if an AABB (Axis Aligned Bounding Box) is visible
    bool IsBoxVisible(const glm::vec3& min, const glm::vec3& max) const {
        for (const auto& plane : planes) {
            // Find the point on the box closest to the plane normal
            glm::vec3 positiveVertex = min;
            if (plane.normal.x >= 0) positiveVertex.x = max.x;
            if (plane.normal.y >= 0) positiveVertex.y = max.y;
            if (plane.normal.z >= 0) positiveVertex.z = max.z;

            // If the "most inside" point is still outside (behind plane), cull it.
            if (glm::dot(plane.normal, positiveVertex) + plane.distance < 0) {
                return false;
            }
        }
        return true;
    }
};