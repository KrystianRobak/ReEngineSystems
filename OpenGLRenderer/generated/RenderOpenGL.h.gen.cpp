// Auto-generated reflection file for RenderOpenGL.h
#include "RenderOpenGL.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {


struct RenderOpenGL_AutoRegister {
    RenderOpenGL_AutoRegister() {
        Reflection::ClassInfo ci;
        ci.name = "RenderOpenGL";
        ci.fullName = "RenderOpenGL";
        ci.module = "/Script/GeneratedModule";
        ci.size = sizeof(RenderOpenGL);
        ci.category = Reflection::TypeCategory::Class;
        ci.isClass = true;
        ci.isStruct = false;
        ci.construct = []() -> void* { return new RenderOpenGL(); };
        ci.destruct = [](void* p) { delete static_cast<RenderOpenGL*>(p); };
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static RenderOpenGL_AutoRegister _renderopengl_autoreg;

} // namespace ReflectionGenerated

