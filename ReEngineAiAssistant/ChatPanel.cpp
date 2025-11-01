#include "ChatPanel.h"

void ChatPanel::OnInit()
{
    connection.ConnectionInit();
    chatLog.push_back("System: Connected to AI assistant.");
}

void ChatPanel::Render()
{
    ImGui::Begin("AI Assistant");

    ImGui::Text("Welcome to the AI Assistant!");
    ImGui::Separator();

    // Display chat log
    ImGui::BeginChild("ChatScrollRegion", ImVec2(0, -40), true);
    for (const auto& msg : chatLog)
        ImGui::TextWrapped("%s", msg.c_str());
    ImGui::EndChild();

    // Handle new messages from Python
    while (connection.HasNewMessage()) {
        std::string newMsg = connection.PopMessage();
        chatLog.push_back("AI: " + newMsg);
    }

    // Input field
    ImGui::PushItemWidth(-50);
    if (ImGui::InputText("##input", inputBuffer, IM_ARRAYSIZE(inputBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue))
    {
        std::string message = inputBuffer;
        if (!message.empty()) {
            connection.SendMessage(message);
            chatLog.push_back("You: " + message);
            inputBuffer[0] = '\0'; // clear input
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Send")) {
        std::string message = inputBuffer;
        if (!message.empty()) {
            connection.SendMessage(message);
            chatLog.push_back("You: " + message);
            inputBuffer[0] = '\0';
        }
    }

    ImGui::End();
}
