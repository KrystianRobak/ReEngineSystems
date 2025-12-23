// Auto-generated reflection file for WorldGenSystem.h
#include "WorldGenSystem.h"
#include "ReflectionEngine.h"
#include <cstddef>

namespace ReflectionGenerated {

static Reflection::FunctionPtr WorldGenSystem_GenerateTerrain_Hook = nullptr;

static void WorldGenSystem_GenerateTerrain_Invoke(void* instance, void** args, void* ret) {
    if (WorldGenSystem_GenerateTerrain_Hook) {
        WorldGenSystem_GenerateTerrain_Hook(instance, args, ret);
        return;
    }
    auto* obj = static_cast<WorldGenSystem*>(instance);
    obj->GenerateTerrain(*static_cast<int*>(args[0]), *static_cast<int*>(args[1]), *static_cast<float*>(args[2]), *static_cast<float*>(args[3]));
}

static std::vector<Reflection::ReflectedFunction> WorldGenSystem_Functions;
static std::vector<Reflection::ReflectedVariable> WorldGenSystem_Variables;
static std::vector<Reflection::BaseClassInfo> WorldGenSystem_Bases;
struct WorldGenSystem_AutoRegister {
    WorldGenSystem_AutoRegister() {
        Reflection::ClassInfo ci;
        ci.name = "WorldGenSystem";
        ci.fullName = "WorldGenSystem";
        ci.module = "/Script/GeneratedModule";
        ci.size = sizeof(WorldGenSystem);
        ci.category = Reflection::TypeCategory::Class;
        ci.isClass = true;
        ci.isStruct = false;
        ci.construct = []() -> void* { return new WorldGenSystem(); };
        ci.destruct = [](void* p) { delete static_cast<WorldGenSystem*>(p); };
        WorldGenSystem_Bases.clear();
        if (auto* __base = Reflection::Registry::Instance().FindClass("System"))
            WorldGenSystem_Bases.push_back(Reflection::BaseClassInfo{ __base, (reinterpret_cast<std::size_t>(static_cast<System*>(reinterpret_cast<WorldGenSystem*>(1))) - 1) });
        ci.bases = WorldGenSystem_Bases;
        WorldGenSystem_Functions.clear();
        {
            auto* retType = Reflection::Registry::Instance().GetOrCreateType("void");
            std::vector<const Reflection::TypeInfo*> paramTypes;
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("int"));
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("int"));
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("float"));
            paramTypes.push_back(Reflection::Registry::Instance().GetOrCreateType("float"));
            Reflection::ReflectedFunction rf = {
                "GenerateTerrain", "public",
                false, false, false,
                retType,
                paramTypes,
                &WorldGenSystem_GenerateTerrain_Invoke
            };
            WorldGenSystem_Functions.push_back(std::move(rf));
        }
        ci.functions = WorldGenSystem_Functions;
        WorldGenSystem_Variables.clear();
        auto* vType = Reflection::Registry::Instance().GetOrCreateType("std::vector<std::basic_string<char>>");
        {
            Reflection::ReflectedVariable rv = {
                "ComponentsToRegister", "public",
                false,
                offsetof(WorldGenSystem, ComponentsToRegister),
                vType
            };
            WorldGenSystem_Variables.push_back(std::move(rv));
        }
        ci.variables = WorldGenSystem_Variables;
        Reflection::Registry::Instance().RegisterSystem(std::move(ci));
    }
};
static WorldGenSystem_AutoRegister _worldgensystem_autoreg;

} // namespace ReflectionGenerated

