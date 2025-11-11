#pragma once

#include "IViewport.h"
#include <Context/OpenGlFrameBuffer.h>
#include "Commander.h"
#include "ReCamera.h"

class OpenGLViewport : public IViewport {
public:
    OpenGLViewport(int width, int height)
    {
        this->width = width;
        this->height = height;
        framebuffer = std::make_unique<OpenGlFrameBuffer>();
        framebuffer->create_buffers(width, height);

		commander = new Commander();
		commander->Init(1000);

		camera = new Camera();
    }
};