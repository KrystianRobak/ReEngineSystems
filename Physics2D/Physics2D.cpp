#include "Physics2D.h"

#include "Components/Transform.h"
#include <thread>

void Physics2D::Update(float dt)
{

	mEntities;
	for (Entity entity : mEntities)
	{
		Transform* transform = static_cast<Transform*>(engine_->GetComponent(entity, "Transform"));

		glm::vec3 velocity(1.0f, 0.0f, 0.0f);

		transform->position += velocity * dt;

		//LOGF_INFO("Entity: %d has position: x: %.2f | y: %.2f | z: %.2f", entity, transform->position.x, transform->position.y, transform->position.z)
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	LOGF_INFO("Physics2D Update");
	return;
}

void Physics2D::Cleanup()
{
}

void Physics2D::SetupModelAndMesh(const Entity& entity)
{
}

void Physics2D::OnLightEntityAdded()
{
}

void Physics2D::RecompileShader()
{
}

void Physics2D::WindowSizeListener()
{
}

extern "C" {
	REFLECTION_API System* CreateSystem()
	{
		return new Physics2D();
	}

}
