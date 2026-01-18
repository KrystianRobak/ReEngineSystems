// Auto-generated reflection file for Physics3D.h
#include "Physics3D.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {


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
        if (auto* __base = Reflection::Registry::Instance().FindClass("PhysicsWorld"))
            Physics3D_Bases.push_back(Reflection::BaseClassInfo{ __base, (reinterpret_cast<std::size_t>(static_cast<PhysicsWorld*>(reinterpret_cast<Physics3D*>(1))) - 1) });
        ci.bases = Physics3D_Bases;
        Physics3D_Variables.clear();
        auto* vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "ComponentsToRegister", "public",
                false,
                offsetof(Physics3D, ComponentsToRegister),
                vType,
                "Transform, RigidBody, BoxCollider"
            };
            Physics3D_Variables.push_back(std::move(rv));
        }
         vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "SystemsToRunAfter", "public",
                false,
                offsetof(Physics3D, SystemsToRunAfter),
                vType,
                "StateMachineSystem"
            };
            Physics3D_Variables.push_back(std::move(rv));
        }
         vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "WriteComponents", "public",
                false,
                offsetof(Physics3D, WriteComponents),
                vType,
                "Transform, RigidBody"
            };
            Physics3D_Variables.push_back(std::move(rv));
        }
        ci.variables = Physics3D_Variables;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static Physics3D_AutoRegister _physics3d_autoreg;

} // namespace ReflectionGenerated

