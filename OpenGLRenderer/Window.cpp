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
    
    frameBuffer = new OpenGlFrameBuffer();

    frameBuffer->create_buffers(800, 600);

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
	event.SetParam<uint64_t>("TextureId", frameBuffer->get_texture());


    EngineApi_->SendEvent(event);
    LayerManager_->OnUpdate();
}

void Window::PreRender()
{
    RenderCtx->pre_render();

    UICtx->pre_render();

    frameBuffer->bind();
}

void Window::PostRender()
{
    frameBuffer->unbind();

    UICtx->post_render();

    RenderCtx->post_render();
}

void* Window::get_native_window()
{
    return window;
}
