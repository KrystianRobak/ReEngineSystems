#include "Physics2D.h"

void Physics2D::Init(Engine::IEngineApi* engine)
{
	engine_ = engine;
	LOGF_WARN("%s", "Zainicjowalem siê!")
}

void Physics2D::Update(float dt)
{
	//LOGF_ERROR
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
