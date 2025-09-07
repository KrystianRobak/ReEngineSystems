#include "RenderOpenGL.h"

#include "Components/Transform.h"
#include <thread>

void RenderOpenGL::Update(float dt)
{
	mEntities;
	//for (Entity entity : mEntities)
	//{
		/*Transform* transform = static_cast<Transform*>(engine_->GetComponent(entity, "Transform"));
		LOGF_INFO("Entity: %d has position: x: %.2f | y: %.2f | z: %.2f", entity, transform->position.x, transform->position.y, transform->position.z)*/
	//}

	std::vector<RenderCommand> RenderCommands = commander_.ConsumeRenderCommands();

	for(RenderCommand command : RenderCommands)
	{
		auto commandPrimitive = command.GetRenderPrimitive();
		LOGF_INFO("Transform")
	}

	LOGF_INFO("RenderOpenGL Update");

	return;
}

void RenderOpenGL::Cleanup()
{
}

void RenderOpenGL::SetupModelAndMesh(const Entity& entity)
{
}

void RenderOpenGL::OnLightEntityAdded()
{
}

void RenderOpenGL::RecompileShader()
{
}

void RenderOpenGL::WindowSizeListener()
{
}

extern "C" {
	REFLECTION_API System* CreateSystem()
	{
		return new RenderOpenGL();
	}

}
