// Auto-generated reflection file for Physics2D.h
#include "Physics2D.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {


struct Physics2D_AutoRegister {
    Physics2D_AutoRegister() {
        Reflection::ClassInfo ci;
        ci.name = "Physics2D";
        ci.fullName = "Physics2D";
        ci.module = "Physics2D";
        ci.size = sizeof(Physics2D);
        ci.category = Reflection::TypeCategory::Class;
        ci.isClass = true;
        ci.isStruct = false;
        ci.construct = []() -> void* { return new Physics2D(); };
        ci.destruct = [](void* p) { delete static_cast<Physics2D*>(p); };
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static Physics2D_AutoRegister _physics2d_autoreg;

} // namespace ReflectionGenerated

