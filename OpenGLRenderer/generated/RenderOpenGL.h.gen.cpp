// Auto-generated reflection file for RenderOpenGL.h
#include "RenderOpenGL.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {


static std::vector<Reflection::ReflectedVariable> RenderOpenGL_Variables;
static std::vector<Reflection::BaseClassInfo> RenderOpenGL_Bases;
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
        RenderOpenGL_Bases.clear();
        if (auto* __base = Reflection::Registry::Instance().FindClass("RenderSystem"))
            RenderOpenGL_Bases.push_back(Reflection::BaseClassInfo{ __base, (reinterpret_cast<std::size_t>(static_cast<RenderSystem*>(reinterpret_cast<RenderOpenGL*>(1))) - 1) });
        ci.bases = RenderOpenGL_Bases;
        RenderOpenGL_Variables.clear();
        auto* vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "ComponentsToRegister", "public",
                false,
                offsetof(RenderOpenGL, ComponentsToRegister),
                vType,
                "Transform, StaticMesh"
            };
            RenderOpenGL_Variables.push_back(std::move(rv));
        }
        ci.variables = RenderOpenGL_Variables;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static RenderOpenGL_AutoRegister _renderopengl_autoreg;

} // namespace ReflectionGenerated

