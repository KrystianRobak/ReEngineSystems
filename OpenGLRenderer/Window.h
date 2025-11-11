#pragma once
#include <memory>
#include "Window/IWindow.h"

#include <string>

#include "Event.h"
#include "UIComponent.h"

#include "Context/OpenGlContext.h"
#include "Context/UIContext.h"
#include "Context/OpenGlFrameBuffer.h"

class AssetManagerApi;


class Window : public IWindow
{
public:
    Window() : IsRunning(true), window(nullptr) 
    {
        UICtx = std::make_unique<UIContext>();
        RenderCtx = std::make_unique<OpenGlContext>();
		LayerManager_ = std::make_unique<ILayerManager>();
    }

    ~Window();

    bool Init(int width, int height, const std::string& title, Editor::IEngineEditorApi* EngineApi, IApplicationApi* ApplicationApi) override;

    void PreRender() override;

    void Render() override;

    void PostRender() override;

    void* get_native_window() override;

    void set_native_window(void* window) override;

    void on_mode_Changed(Event& event) override;

    void on_resize(int width, int height) override;

    void on_close() override;

    bool is_running()  override { return true; }

private:

    GLFWwindow* window;
    
    std::unique_ptr<UIContext> UICtx;

    std::unique_ptr<OpenGlContext> RenderCtx;

    bool IsRunning;


};

