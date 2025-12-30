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
    obj->AddForceToEntity(*static_cast<Entity*>(args[0]), *static_cast<glm::vec3*>(args[1]));
}

static std::vector<Reflection::ReflectedFunction> Physics3D_Functions;
static std::vector<Reflection::ReflectedVariable> Physics3D_Variables;
static std::vector<Reflection::BaseClassInfo> Physics3D_Bases;
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
        Physics3D_Bases.clear();
        if (auto* __base = Reflection::Registry::Instance().FindClass("System"))
            Physics3D_Bases.push_back(Reflection::BaseClassInfo{ __base, (reinterpret_cast<std::size_t>(static_cast<System*>(reinterpret_cast<Physics3D*>(1))) - 1) });
        ci.bases = Physics3D_Bases;
        Physics3D_Functions.clear();
        {
            auto* retType = Reflection::Registry::Instance().GetOrCreateType("void");
            std::vector<const Reflection::TypeInfo*> paramTypes;
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("Entity"));
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("glm::vec3"));
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
        Physics3D_Variables.clear();
        auto* vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "ComponentsToRegister", "public",
                false,
                offsetof(Physics3D, ComponentsToRegister),
                vType,
                "Transform, RigidBody"
            };
            Physics3D_Variables.push_back(std::move(rv));
        }
         vType = Reflection::Registry::Instance().GetOrCreateType("glm::vec<3, float>");
        {
            Reflection::ReflectedVariable rv = {
                "gravity", "public",
                false,
                offsetof(Physics3D, gravity),
                vType,
                "glm, ::, vec3, (, 0.0f, -, 0.81f, 0.0f, )"
            };
            Physics3D_Variables.push_back(std::move(rv));
        }
        ci.variables = Physics3D_Variables;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static Physics3D_AutoRegister _physics3d_autoreg;

} // namespace ReflectionGenerated

