// Auto-generated reflection file for StateMachineSystem.h
#include "StateMachineSystem.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {


static std::vector<Reflection::ReflectedVariable> StateMachineSystem_Variables;
static std::vector<Reflection::BaseClassInfo> StateMachineSystem_Bases;
struct StateMachineSystem_AutoRegister {
    StateMachineSystem_AutoRegister() {
        Reflection::ClassInfo ci;
        ci.name = "StateMachineSystem";
        ci.fullName = "StateMachineSystem";
        ci.module = "/Script/GeneratedModule";
        ci.size = sizeof(StateMachineSystem);
        ci.category = Reflection::TypeCategory::Class;
        ci.isClass = true;
        ci.isStruct = false;
        ci.construct = []() -> void* { return new StateMachineSystem(); };
        ci.destruct = [](void* p) { delete static_cast<StateMachineSystem*>(p); };
        StateMachineSystem_Bases.clear();
        if (auto* __base = Reflection::Registry::Instance().FindClass("System"))
            StateMachineSystem_Bases.push_back(Reflection::BaseClassInfo{ __base, (reinterpret_cast<std::size_t>(static_cast<System*>(reinterpret_cast<StateMachineSystem*>(1))) - 1) });
        ci.bases = StateMachineSystem_Bases;
        StateMachineSystem_Variables.clear();
        auto* vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "ComponentsToRegister", "public",
                false,
                offsetof(StateMachineSystem, ComponentsToRegister),
                vType,
                "StateMachine, SkeletalMeshComponent"
            };
            StateMachineSystem_Variables.push_back(std::move(rv));
        }
         vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "SystemsToRunAfter", "public",
                false,
                offsetof(StateMachineSystem, SystemsToRunAfter),
                vType
            };
            StateMachineSystem_Variables.push_back(std::move(rv));
        }
         vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "WriteComponents", "public",
                false,
                offsetof(StateMachineSystem, WriteComponents),
                vType,
                "StateMachine, SkeletalMeshComponent, RigidBody"
            };
            StateMachineSystem_Variables.push_back(std::move(rv));
        }
        ci.variables = StateMachineSystem_Variables;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static StateMachineSystem_AutoRegister _statemachinesystem_autoreg;

} // namespace ReflectionGenerated

