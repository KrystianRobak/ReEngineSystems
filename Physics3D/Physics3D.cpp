#include "Physics3D.h"

#include "Components/Transform.h"
#include <thread>

void Physics3D::Update(float dt)
{
	mEntities;
	for (Entity entity : mEntities)
	{
		Transform* transform = static_cast<Transform*>(engine_->GetComponent(entity, "Transform"));

		glm::vec3 velocity(1.0f, 0.0f, 0.0f);

		transform->scale = glm::vec3(1.0f, 1.0f, 1.0f);
		transform->position += velocity * dt;

		engine_->MarkEntityDirty(entity, "Transform");

		//LOGF_INFO("Entity: %d has position: x: %.2f | y: %.2f | z: %.2f", entity, transform->position.x, transform->position.y, transform->position.z)
	}

	for (const auto& [entity, force] : AppliedForces)
	{
		Transform* transform = static_cast<Transform*>(engine_->GetComponent(entity, "Transform"));

		// Simple physics integration: apply force to position
		transform->position.x += force * dt; // Assuming mass = 1 for simplicity
		engine_->MarkEntityDirty(entity, "Transform");
		LOGF_INFO("Applied force to Entity: %d | New position: x: %.2f | y: %.2f | z: %.2f", entity, transform->position.x, transform->position.y, transform->position.z);

	}

	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	//LOGF_INFO("Physics3D Update");
	return;
}

void Physics3D::Cleanup()
{
}

void Physics3D::AddForceToEntity(Entity entity, int xForce)
{
	AppliedForces.push_back(std::make_tuple(entity, xForce));
}

void Physics3D::SetupModelAndMesh(const Entity& entity)
{
}

void Physics3D::OnLightEntityAdded()
{
}

void Physics3D::RecompileShader()
{
}

void Physics3D::WindowSizeListener()
{
}

extern "C" {
	REFLECTION_API System* CreateSystem()
	{
		return new Physics3D();
	}
}
