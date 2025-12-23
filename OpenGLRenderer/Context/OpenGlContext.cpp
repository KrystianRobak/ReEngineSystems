#include "../Context/OpenGlContext.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include "Engine/Core/Coordinator/Coordinator.h"
#include "Window/IWindow.h"
#include <Logger.h>

static void on_window_size_callback(GLFWwindow* window, int width, int height)
{
    auto pWindow = static_cast<IWindow*>(glfwGetWindowUserPointer(window));
    pWindow->on_resize(width, height);
}

static void on_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    //std::shared_ptr<Coordinator> coordinator = Coordinator::GetCoordinator();

    //if ((key >= 65 && key <= 90) && (action == GLFW_REPEAT || action == GLFW_PRESS || action == GLFW_RELEASE))
    //{
    //    Event KeyEvent(Events::Window::INPUT);
    //    KeyEvent.SetParam<int>("InputKey", key);
    //    KeyEvent.SetParam<int>("InputAction", action);
    //    coordinator->SendEvent(KeyEvent);
    //}
}

static void OnFileDropCallback(GLFWwindow* window, int count, const char** paths)
{
    auto pWindow = static_cast<IWindow*>(glfwGetWindowUserPointer(window));
    for (int i = 0; i < count; i++)
    {
        Event FileDropEvent(Events::Window::FILE_DROPPED);
        FileDropEvent.SetParam<std::string>("FilePath", std::string(paths[i]));
        pWindow->EngineApi_->SendEvent(FileDropEvent);
    }
}

static inline void glfw_error_callback(int error, const char* description)
{
   LOGF_ERROR("GLFW Error %d : %s",  error, description)
}

bool OpenGlContext::init(IWindow* window)
{
    RenderContext::init(window);




    if (!glfwInit()) {

    }
    glfwSetErrorCallback(glfw_error_callback);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    // Create a windowed mode window and its OpenGL context
    auto glwindow = glfwCreateWindow(window->width, window->height, "Triangle", NULL, NULL);
    window->set_native_window(glwindow);
    if (!window) {
        glfwTerminate();
    }

    bool bIsWindowFocused = true;

    glfwSetWindowUserPointer(glwindow, window);

    // Make the window's context current
    glfwMakeContextCurrent(glwindow);
    glfwSetInputMode(glwindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    // Initialize GLEW
    glewExperimental = true;

    if (glewInit() != GLEW_OK) {
    }
    glEnable(GL_DEPTH_TEST);

    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    glClearColor(0.0f, 0.0f, 0.4f, 0.0f);
    /* Initialize the library */
    glfwSetWindowSizeCallback(glwindow, on_window_size_callback);
    glfwSetKeyCallback(glwindow, on_key_callback);
    glfwSetDropCallback(glwindow, OnFileDropCallback);
    /*glfwSetScrollCallback(glWindow, on_scroll_callback);
    * 
    
    glfwSetWindowCloseCallback(glWindow, on_window_close_callback);*/
    return true;
}




void OpenGlContext::pre_render()
{
    glViewport(0, 0, window->width, window->height);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGlContext::post_render()
{
    glfwSwapBuffers((GLFWwindow*)window->get_native_window());
    glfwPollEvents();
}

void OpenGlContext::end()
{
    glfwDestroyWindow((GLFWwindow*)window->get_native_window());
    glfwTerminate();
}