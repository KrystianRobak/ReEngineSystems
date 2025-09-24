// Auto-generated reflection file for Physics3D.h
#include "Physics3D.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {

static Reflection::FunctionPtr Physics3D_AddForceToEntity_Hook = nullptr;

static void Physics3D_AddForceToEntity_Invoke(void* instance, void** args, void* ret) {
    if (Physics3D_AddForceToEntity_Hook) {
        Physics3D_AddForceToEntity_Hook(instance, args, ret);
        return;
    }
    auto* obj = static_cast<Physics3D*>(instance);
    obj->AddForceToEntity(*static_cast<int*>(args[0]), *static_cast<int*>(args[1]));
}

static std::vector<Reflection::ReflectedFunction> Physics3D_Functions;
struct Physics3D_AutoRegister {
    Physics3D_AutoRegister() {
        Reflection::ClassInfo ci;
        ci.name = "Physics3D";
        ci.fullName = "Physics3D";
        ci.module = "/Script/GeneratedModule";
        ci.size = sizeof(Physics3D);
        ci.category = Reflection::TypeCategory::Class;
        ci.isClass = true;
        ci.isStruct = false;
        ci.construct = []() -> void* { return new Physics3D(); };
        ci.destruct = [](void* p) { delete static_cast<Physics3D*>(p); };
        Physics3D_Functions.clear();
        {
            auto* retType = Reflection::Registry::Instance().GetOrCreateType("void");
            std::vector<const Reflection::TypeInfo*> paramTypes;
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("int"));
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("int"));
            Reflection::ReflectedFunction rf = {
                "AddForceToEntity", "public",
                false, false, false,
                retType,
                paramTypes,
                &Physics3D_AddForceToEntity_Invoke
            };
            Physics3D_Functions.push_back(std::move(rf));
        }
        ci.functions = Physics3D_Functions;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static Physics3D_AutoRegister _physics3d_autoreg;

} // namespace ReflectionGenerated

