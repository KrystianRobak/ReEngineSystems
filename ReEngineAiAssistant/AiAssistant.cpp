#include "AiAssistant.h"

#include "Components/Transform.h"
#include <thread>
#include <AiAssistantLayer.h>


void AiAssistant::Update(float dt)
{


	return;
}

void AiAssistant::Cleanup()
{
}

void AiAssistant::CreateUi()
{
	layerManager_->AddLayerThreadSafe<AiAssistantLayer>();
}


extern "C" {
	REFLECTION_API System* CreateSystem()
	{
		return new AiAssistant();
	}
}
