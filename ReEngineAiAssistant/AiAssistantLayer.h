#pragma once

#include "ILayer.h"
#include <ChatPanel.h>

class AiAssistantLayer : public ILayer
{
	public:
	AiAssistantLayer()
	{
		name = "AI Assistant";
		uiComponents.push_back(std::make_unique<ChatPanel>());
	}
	const char* GetName() const override
	{
		return name;
	}
	void OnEvent(class Event& event) override
	{
	}
};

