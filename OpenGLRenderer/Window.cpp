#include "Window.h"


bool Window::Init(int width, int height, const std::string& title, Editor::IEngineEditorApi* EngineApi)
{
   
    this->width = width;
    this->height = height;
    this->title = title;

    EngineApi_ = EngineApi;

    RenderCtx->init(this);

    UICtx->init(this);

    LayerManager_ = std::make_unique<ILayerManager>();
    LayerManager_->Init(EngineApi_, ImGui::GetCurrentContext());
    
    framebuffer = std::make_unique<OpenGlFrameBuffer>();

    framebuffer->create_buffers(width, height);

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
    Event event(Events::Engine::Renderer::RENDER_FINISHED);
	event.SetParam<uint64_t>("TextureId", framebuffer->get_texture());


    EngineApi_->SendEvent(event);
	LayerManager_->ProcessPendingOps();
    LayerManager_->OnUpdate();
}

void Window::PreRender()
{
    RenderCtx->pre_render();

    UICtx->pre_render();

    framebuffer->bind();
}

void Window::PostRender()
{
    framebuffer->unbind();

    UICtx->post_render();

    RenderCtx->post_render();
}

void* Window::get_native_window()
{
    return window;
}
