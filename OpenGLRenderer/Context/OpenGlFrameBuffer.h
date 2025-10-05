#pragma once
#include "Window/FrameBuffer.h"

class OpenGlFrameBuffer : public FrameBuffer
{
public:

    void create_buffers(int32_t width, int32_t height) override;

    void delete_buffers() override;

    void bind() override;

    void unbind() override;

    uint32_t get_texture() override;
};

