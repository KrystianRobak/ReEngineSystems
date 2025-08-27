#include "RenderOpenGL.h"

#include "Components/Transform.h"

void RenderOpenGL::Init(Engine::IEngineApi* engine)
{
	engine_ = engine;
	LOGF_WARN("%s", "Zainicjowalem siê!")
}

void RenderOpenGL::Update(float dt)
{
	mEntities;
	for (Entity entity : mEntities)
	{
		Transform* transform = static_cast<Transform*>(engine_->GetComponent(entity, "Transform"));
		//LOGF_INFO("Entity: %d has position: x: %d | y: %d | z: %d", entity, transform->position.x, transform->position.y, transform->position.z)
	}
	
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
