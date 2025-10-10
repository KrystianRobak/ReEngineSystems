#include "UIContext.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

bool UIContext::init(IWindow* window)
{
    RenderContext::init(window);

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 410";

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    if (ImGui::GetCurrentContext() == nullptr)
        ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

    auto& colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.1f, 0.1f, 1.0f };

    colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
    colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
    colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

    colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
    colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
    colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

    colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
    colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
    colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

    colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
    colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.38f, 0.38f, 1.0f };
    colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.28f, 0.28f, 1.0f };
    colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };

    colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
    colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)window->get_native_window(), true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    int w, h, channels;
    unsigned char* data = stbi_load("icon.png", &w, &h, &channels, 4);
    if (data)
    {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);

        appIcon = (ImTextureID)(intptr_t)tex;
    }


    return true;
}

void UIContext::pre_render()
{
    if (ImGui::GetCurrentContext() == nullptr) return; // Guard: skip if context is missing

    // Start frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Main invisible full window
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiID dockSpaceId = ImGui::GetID("InvisibleWindowDockSpace");
    float titleBarHeight = 20.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + titleBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - (titleBarHeight*2)));

    ImGui::Begin("InvisibleWindow", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    

    // ---------------- CUSTOM TITLE BAR ----------------
    
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, titleBarHeight));

    ImGuiWindowFlags titleFlags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));

    ImGui::Begin("##TitleBar", nullptr, titleFlags); // no label shown

    // Left side: Icon + Title
    if (appIcon)
    {
        ImGui::Image(appIcon, ImVec2(20, 20));
        ImGui::SameLine();
    }
    ImGui::TextUnformatted("ReEngine");

    // Right side: Control buttons
    float buttonSize = 20.0f;
    float spacing = 4.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - 10 - (buttonSize + spacing) * 3);

    if (ImGui::Button("_", ImVec2(buttonSize, buttonSize))) {
        glfwIconifyWindow((GLFWwindow*)window->get_native_window());
    }
    ImGui::SameLine();
    if (ImGui::Button("[]", ImVec2(buttonSize, buttonSize))) {
        auto* win = (GLFWwindow*)window->get_native_window();
        if (glfwGetWindowAttrib(win, GLFW_MAXIMIZED))
            glfwRestoreWindow(win);
        else
            glfwMaximizeWindow(win);
    }
    ImGui::SameLine();
    if (ImGui::Button("X", ImVec2(buttonSize, buttonSize))) {
        glfwSetWindowShouldClose((GLFWwindow*)window->get_native_window(), GLFW_TRUE);
    }

    // Dragging by holding anywhere in title bar except buttons
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        auto* win = (GLFWwindow*)window->get_native_window();
        static double lastX, lastY;
        static bool dragging = false;

        double mouseX, mouseY;
        glfwGetCursorPos(win, &mouseX, &mouseY);

        int winX, winY;
        glfwGetWindowPos(win, &winX, &winY);

        if (!dragging) {
            lastX = mouseX;
            lastY = mouseY;
            dragging = true;
        }

        glfwSetWindowPos(win, (int)(winX + mouseX - lastX), (int)(winY + mouseY - lastY));

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            dragging = false;
    }

    //// Draw bottom border line
    //ImVec2 winPos = ImGui::GetWindowPos();
    //ImVec2 winSize = ImGui::GetWindowSize();
    //ImGui::GetWindowDrawList()->AddLine(
    //    ImVec2(winPos.x, winPos.y + winSize.y),
    //    ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
    //    IM_COL32(80, 80, 80, 255), 25.0f);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    // ---------------- END TITLE BAR ----------------

    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void UIContext::post_render()
{
    if (ImGui::GetCurrentContext() == nullptr) return; // Guard: skip if context is missing

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
    ImGui::EndFrame();
}

void UIContext::end()
{
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::DestroyContext();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
}