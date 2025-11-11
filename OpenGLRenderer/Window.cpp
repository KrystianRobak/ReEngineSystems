#include "Window.h"


bool Window::Init(int width, int height, const std::string& title, Editor::IEngineEditorApi* EngineApi, IApplicationApi* ApplicationApi)
{
   
    this->width = width;
    this->height = height;
    this->title = title;

    EngineApi_ = EngineApi;
	ApplicationApi_ = ApplicationApi;

    RenderCtx->init(this);

    UICtx->init(this);

    LayerManager_ = std::make_unique<ILayerManager>();
    LayerManager_->Init(EngineApi_, ApplicationApi_, ImGui::GetCurrentContext());

    return IsRunning;
}

Window::~Window()
{
    UICtx->end();

    RenderCtx->end();
}

void Window::set_native_window(void* window)
{
    this->window = (GLFWwindow*)window;
}

void Window::on_mode_Changed(Event& event) {
    MenuType key = static_cast<MenuType>(event.GetParam<int>("MenuType"));
    this->CurrentMode = key;
}

void Window::on_resize(int width, int height)
{
    this->width = width;
    this->height = height;

    Render();
}

void Window::on_close()
{
    IsRunning = false;
}

void Window::Render()
{
	LayerManager_->ProcessPendingOps();
    LayerManager_->OnUpdate();
}

void Window::PreRender()
{
    RenderCtx->pre_render();

    UICtx->pre_render();
}

void Window::PostRender()
{
    UICtx->post_render();

    RenderCtx->post_render();
}

void* Window::get_native_window()
{
    return window;
}
