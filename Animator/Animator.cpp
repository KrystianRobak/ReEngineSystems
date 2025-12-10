#include "Animator.h"

#include <thread>

void Animator::Update(float dt)
{
	
	return;
}

void Animator::Cleanup()
{
}


extern "C" {
	REFLECTION_API System* CreateSystem()
	{
		return new Animator();
	}
}
