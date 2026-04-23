# ReEngineSystems

Loadable runtime systems for `ReEngine`.

This directory contains the separately compiled engine modules used by the main runtime. These projects are not standalone applications. They build DLLs that extend the core engine with rendering, physics, animation, and other optional systems loaded at startup.

ReEngine Systems can be found here: https://github.com/KrystianRobak/ReEngine

## What This Folder Is For

- Keep the main engine focused on ECS coordination, asset management, reflection, scene handling, and editor/runtime integration.
- Move backend or feature-specific runtime logic into replaceable modules.
- Allow a project configuration to choose which renderer, physics backend, and extra systems should be loaded.

In practice, `ReEngine`, `ReEngineEditor`, and `ReEngineRunner` depend on the DLLs produced here.

## How It Fits Into The Engine

```text
Game DLL / Editor / Runner
    |
    v
ReEngine.dll
    |
    +-- Coordinator
    +-- AssetManager
    +-- SceneManager
    +-- reflection registry
    |
    v
System DLLs from ReEngineSystems/
    +-- renderer
    +-- physics
    +-- animation
    +-- optional modules
```

The systems in this solution are loaded by the main engine and executed through the engine's system graph and dependency scheduling.

## Included Modules

- `OpenGLRenderer`
  - Main rendering backend used by the engine runtime.
  - Owns viewport/framebuffer rendering and OpenGL-side rendering work.

- `Physics3D`
  - 3D physics backend.
  - Integrates PhysX and writes simulation results back into ECS components.

- `Animator`
  - Skeletal animation runtime system.
  - Updates animation state and bone data used by rendering.

- `StateMachineSystem`
  - Animation/state-machine logic.
  - Handles graph-driven clip switching and parameter-based transitions.

- `ReEngineAiAssistant`
  - Optional/experimental module.
  - Not part of the minimum runtime path.

- `ReEngineModule1`
  - Example custom engine-side module.
  - Used as an additional non-core system module for the engine.

## Build Output

Build this solution from:

- `ReEngineSystems/ReEngineSystems.sln`

The resulting DLLs are intended to be copied or emitted into the engine's systems output directory, typically under:

- `ReEngine/bin/<Configuration>/Systems`

Those binaries are then loaded by the main engine based on the active project configuration.

## Important Notes

- These projects are engine modules, not end-user game projects.
- They rely on the main `ReEngine` codebase for headers, shared runtime types, reflection data, and interfaces.
- If the main engine solution is out of date, the systems here may fail to build or load correctly.
- Some projects may still contain machine-specific paths until the repository is fully path-cleaned.

## Summary

`ReEngineSystems` is the module layer of the repository. It contains the runtime systems that plug into the core engine and provide the concrete rendering, physics, animation, and optional feature implementations used by the main engine.
