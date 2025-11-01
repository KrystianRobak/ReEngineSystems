#pragma once

#include "UIComponent.h"
#include "Connection.h"


class ChatPanel : public UIComponent
{
public:
    void OnInit() override;
    void Render();

private:
    PyConnection connection;
    std::vector<std::string> chatLog;
    char inputBuffer[512] = "";
};
