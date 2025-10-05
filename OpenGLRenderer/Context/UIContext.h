#pragma once
#include "Window/RenderContext.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

class UIContext : public RenderContext
{

public:

    bool init(IWindow* window) override;

    void pre_render() override;

    void post_render() override;

    void end() override;

private:
    ImTextureID appIcon = nullptr;
};

