// Auto-generated reflection file for Animator.h
#include "Animator.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {


static std::vector<Reflection::ReflectedVariable> Animator_Variables;
static std::vector<Reflection::BaseClassInfo> Animator_Bases;
struct Animator_AutoRegister {
    Animator_AutoRegister() {
        Reflection::ClassInfo ci;
        ci.name = "Animator";
        ci.fullName = "Animator";
        ci.module = "Animator";
        ci.size = sizeof(Animator);
        ci.category = Reflection::TypeCategory::Class;
        ci.isClass = true;
        ci.isStruct = false;
        ci.construct = []() -> void* { return new Animator(); };
        ci.destruct = [](void* p) { delete static_cast<Animator*>(p); };
        Animator_Bases.clear();
        if (auto* __base = Reflection::Registry::Instance().FindClass("System"))
            Animator_Bases.push_back(Reflection::BaseClassInfo{ __base, (reinterpret_cast<std::size_t>(static_cast<System*>(reinterpret_cast<Animator*>(1))) - 1) });
        ci.bases = Animator_Bases;
        Animator_Variables.clear();
        auto* vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "ComponentsToRegister", "public",
                false,
                offsetof(Animator, ComponentsToRegister),
                vType,
                "SkeletalMeshComponent, StateMachine"
            };
            Animator_Variables.push_back(std::move(rv));
        }
         vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "SystemsToRunAfter", "public",
                false,
                offsetof(Animator, SystemsToRunAfter),
                vType,
                "StateMachineSystem, Physics3D"
            };
            Animator_Variables.push_back(std::move(rv));
        }
         vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "WriteComponents", "public",
                false,
                offsetof(Animator, WriteComponents),
                vType,
                "SkeletalMeshComponent"
            };
            Animator_Variables.push_back(std::move(rv));
        }
        ci.variables = Animator_Variables;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static Animator_AutoRegister _animator_autoreg;

} // namespace ReflectionGenerated

