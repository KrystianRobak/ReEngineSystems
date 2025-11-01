// Auto-generated reflection file for AiAssistant.h
#include "AiAssistant.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {

static Reflection::FunctionPtr AiAssistant_CreateUi_Hook = nullptr;

static void AiAssistant_CreateUi_Invoke(void* instance, void** args, void* ret) {
    if (AiAssistant_CreateUi_Hook) {
        AiAssistant_CreateUi_Hook(instance, args, ret);
        return;
    }
    auto* obj = static_cast<AiAssistant*>(instance);
    obj->CreateUi();
}

static std::vector<Reflection::ReflectedFunction> AiAssistant_Functions;
static std::vector<Reflection::ReflectedVariable> AiAssistant_Variables;
static std::vector<Reflection::BaseClassInfo> AiAssistant_Bases;
struct AiAssistant_AutoRegister {
    AiAssistant_AutoRegister() {
        Reflection::ClassInfo ci;
        ci.name = "AiAssistant";
        ci.fullName = "AiAssistant";
        ci.module = "/Script/GeneratedModule";
        ci.size = sizeof(AiAssistant);
        ci.category = Reflection::TypeCategory::Class;
        ci.isClass = true;
        ci.isStruct = false;
        ci.construct = []() -> void* { return new AiAssistant(); };
        ci.destruct = [](void* p) { delete static_cast<AiAssistant*>(p); };
        AiAssistant_Bases.clear();
        if (auto* __base = Reflection::Registry::Instance().FindClass("System"))
            AiAssistant_Bases.push_back(Reflection::BaseClassInfo{ __base, (reinterpret_cast<std::size_t>(static_cast<System*>(reinterpret_cast<AiAssistant*>(1))) - 1) });
        ci.bases = AiAssistant_Bases;
        AiAssistant_Functions.clear();
        {
            auto* retType = Reflection::Registry::Instance().GetOrCreateType("void");
            std::vector<const Reflection::TypeInfo*> paramTypes;
            Reflection::ReflectedFunction rf = {
                "CreateUi", "public",
                false, false, false,
                retType,
                paramTypes,
                &AiAssistant_CreateUi_Invoke
            };
            AiAssistant_Functions.push_back(std::move(rf));
        }
        ci.functions = AiAssistant_Functions;
        AiAssistant_Variables.clear();
        auto* vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "ComponentsToRegister", "public",
                false,
                offsetof(AiAssistant, ComponentsToRegister),
                vType
            };
            AiAssistant_Variables.push_back(std::move(rv));
        }
        ci.variables = AiAssistant_Variables;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static AiAssistant_AutoRegister _aiassistant_autoreg;

} // namespace ReflectionGenerated

