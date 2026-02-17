#include "Physics3D.h"
#include "StaticMeshData.h"
#include "Components/Transform.h"
#include "Components/RigidBody.h"   
#include "Components/BoxCollider.h" 
#include "Components/StaticMesh.h"
#include <iostream>
#include <algorithm>

#include <MeshCollider.h>

// PhysX Error Callback
class UserErrorCallback : public PxErrorCallback {
public:
    virtual void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override {
        std::cerr << "[PhysX Error] " << message << std::endl;
    }
} gErrorCallback;

// PhysX Allocator
static PxDefaultAllocator gAllocator;

void Physics3D::OnInit() {
    InitPhysX();
}

void Physics3D::InitPhysX() {
    // 1. Foundation
    mFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    if (!mFoundation) {
        std::cerr << "PxCreateFoundation failed!" << std::endl;
        return;
    }

    // 2. PVD (PhysX Visual Debugger) - Connects to the external PhysX GUI if running
    mPvd = PxCreatePvd(*mFoundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    mPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);

    // 3. Physics
    mPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *mFoundation, PxTolerancesScale(), true, mPvd);

    // --- DODAJ TÊ LINIJKÊ ---
    if (!PxInitExtensions(*mPhysics, nullptr)) {
        std::cerr << "PxInitExtensions failed!" << std::endl;
    }

    // 4. Dispatcher (Worker Threads)
    mDispatcher = PxDefaultCpuDispatcherCreate(2);

    // 5. Scene
    PxSceneDesc sceneDesc(mPhysics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = mDispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;

    mScene = mPhysics->createScene(sceneDesc);

    // 6. Default Material (StaticFriction, DynamicFriction, Restitution)
    mDefaultMaterial = mPhysics->createMaterial(0.5f, 0.5f, 0.6f);

    std::cout << "PhysX Initialized Successfully." << std::endl;
}

void Physics3D::Cleanup() {
    // Release all actors first
    mEntityActorMap.clear();

    PX_RELEASE(mScene);
    PX_RELEASE(mDispatcher);
    PX_RELEASE(mPhysics);

    if (mPvd) {
        PxPvdTransport* transport = mPvd->getTransport();
        mPvd->release();
        mPvd = nullptr;
        PX_RELEASE(transport);
    }

    PX_RELEASE(mFoundation);
}

void Physics3D::Update(float dt) {
    if (!mScene) return;

    // Clamp dt to prevent explosion on lag spikes
    dt = std::min(dt, 0.033f);

    // 1. Auto-Fit Logic (Preserved from your original code)
    FitCollidersToMeshes();

    // 2. Sync ECS -> PhysX (Create actors for new entities)
    for (Entity entity : mEntities) {
        if (mEntityActorMap.find(entity) == mEntityActorMap.end()) {

            bool hasTransform = engine_->HasComponent(entity, "Transform");
            bool hasBox = engine_->HasComponent(entity, "BoxCollider");
            bool hasMesh = engine_->HasComponent(entity, "MeshCollider");

            // Create actor if it has a Transform AND (BoxCollider OR MeshCollider)
            if (hasTransform && (hasBox || hasMesh)) {
                CreateActorForEntity(entity);
            }
        }
    }

    // 3. Handle Destroyed Entities (Cleanup actors)
    auto it = mEntityActorMap.begin();
    while (it != mEntityActorMap.end()) {
        if (!engine_->IsEntityAlive(it->first)) {
            mScene->removeActor(*it->second);
            it->second->release();
            it = mEntityActorMap.erase(it);
        }
        else {
            ++it;
        }
    }

    SyncECSToPhysX();

    // 4. Simulate
    mScene->simulate(dt);
    mScene->fetchResults(true);

    // 5. Sync PhysX -> ECS (Update Transform components)
    for (auto& pair : mEntityActorMap) {
        Entity entity = pair.first;
        PxRigidActor* actor = pair.second;

        // Static objects don't move, skip them
        if (actor->getType() == PxActorType::eRIGID_STATIC) continue;

        PxRigidDynamic* dynamic = (PxRigidDynamic*)actor;

        // Sleeping optimization
        if (dynamic->isSleeping()) continue;

        PxTransform t = dynamic->getGlobalPose();

        Transform* tx = (Transform*)engine_->GetComponentForWrite(entity, "Transform");
        if (tx) {
            tx->position = ToGlmVec3(t.p);
            tx->rotation = ToGlmQuat(t.q);
            engine_->MarkEntityDirty(entity, "Transform");
        }

        // Optional: Update RigidBody velocity for gameplay logic
        RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
        if (rb) {
            rb->velocity = ToGlmVec3(dynamic->getLinearVelocity());
            rb->angularVelocity = ToGlmVec3(dynamic->getAngularVelocity());
        }
    }
}

void Physics3D::CreateActorForEntity(Entity entity) {
    auto* tx = (Transform*)engine_->GetComponent(entity, "Transform");
    if (!tx) return;

    // 1. Get Components
   
    bool hasbox = engine_->HasComponent(entity, "BoxCollider");
    bool hasMeshCol = engine_->HasComponent(entity, "MeshCollider");

    auto* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");

    // Exit if no collision data exists
    if (!hasbox && !hasMeshCol) return;

    // 2. Prepare Transform (PhysX requires normalized quaternions)
    glm::quat q = tx->rotation;
    if (glm::length(q) < 0.0001f) {
        q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    else {
        q = glm::normalize(q);
    }

    PxTransform pxTransform(ToPxVec3(tx->position), ToPxQuat(q));
    PxRigidActor* actor = nullptr;

    // =========================================================
    // OPTION A: MESH COLLIDER (Static Maps / Buildings)
    // =========================================================
    if (hasMeshCol) {

        auto* meshCol = (MeshCollider*)engine_->GetComponent(entity, "MeshCollider");
        // Triangle Meshes MUST be static in PhysX. 
        // We ignore RigidBody dynamic settings here to prevent crashes.
        PxRigidStatic* staticActor = mPhysics->createRigidStatic(pxTransform);

        // Check if we have the cooked binary blob from AssetSerializer
        if (!meshCol->meshData->physicsData.empty()) {

            // A. Create Input Stream from the loaded memory blob
            PxDefaultMemoryInputData inputData(
                meshCol->meshData->physicsData.data(),
                (PxU32)meshCol->meshData->physicsData.size()
            );

            // B. Create the Mesh Object directly (No Cooking overhead!)
            PxTriangleMesh* triMesh = mPhysics->createTriangleMesh(inputData);

            if (triMesh) {
                // Apply Transform Scaling
                PxMeshScale scale(ToPxVec3(tx->scale), PxQuat(PxIdentity));
                PxTriangleMeshGeometry triGeom(triMesh, scale);

                // Create and Attach Shape
                PxShape* shape = mPhysics->createShape(triGeom, *mDefaultMaterial);
                staticActor->attachShape(*shape);

                // Cleanup: The shape holds the reference now, so we release our local pointer
                triMesh->release();
                shape->release();
            }
            else {
                std::cerr << "[Physics] Failed to create triangle mesh from binary data." << std::endl;
            }
        }
        else {
            std::cerr << "[Physics] MeshCollider found but 'physicsData' is empty. Did you re-import the asset?" << std::endl;
        }

        actor = staticActor;
    }
    // =========================================================
    // OPTION B: BOX COLLIDER (Dynamic Props / Players)
    // =========================================================
    else if (hasbox) {

        auto* box = (BoxCollider*)engine_->GetComponent(entity, "BoxCollider");
        // Calculate Half-Extents (PhysX uses half-sizes)
        glm::vec3 halfExtents = (box->size * tx->scale) * 0.5f;
        halfExtents = glm::max(halfExtents, glm::vec3(0.01f)); // Prevent zero-size crash

        PxBoxGeometry geometry(halfExtents.x, halfExtents.y, halfExtents.z);

        // Determine if Static or Dynamic
        bool isStatic = (rb == nullptr) || (rb->isStatic);

        if (isStatic) {
            actor = mPhysics->createRigidStatic(pxTransform);
        }
        else {
            PxRigidDynamic* dynamic = mPhysics->createRigidDynamic(pxTransform);

            // Mass & Inertia
            PxRigidBodyExt::updateMassAndInertia(*dynamic, rb->mass);

            // Velocity
            dynamic->setLinearVelocity(ToPxVec3(rb->velocity));

            // Locking
            if (rb->lockAngular) {
                dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, true);
                dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, true);
                dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, true);
            }

            // Damping (Drag)
            dynamic->setLinearDamping(0.01f);
            dynamic->setAngularDamping(0.05f);

            actor = dynamic;
        }

        if (actor) {
            PxShape* shape = mPhysics->createShape(geometry, *mDefaultMaterial);

            // Apply Box Offset
            PxTransform offset(ToPxVec3(box->offset));
            shape->setLocalPose(offset);

            actor->attachShape(*shape);
            shape->release();
        }
    }

    // 3. Final Registration
    if (actor) {
        // Store Entity ID in userData for raycasting identification
        actor->userData = (void*)(uintptr_t)entity;

        mScene->addActor(*actor);
        mEntityActorMap[entity] = actor;
    }
}

void Physics3D::DestroyActorForEntity(Entity entity)
{
}

void Physics3D::SyncECSToPhysX() {
    for (auto& pair : mEntityActorMap) {
        Entity entity = pair.first;
        PxRigidActor* actor = pair.second;

        if (actor->getType() == PxActorType::eRIGID_STATIC) continue;

        PxRigidDynamic* dynamic = (PxRigidDynamic*)actor;

        auto* tx = (Transform*)engine_->GetComponent(entity, "Transform");
        auto* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");

        if (!tx || !rb) continue;


        glm::vec3 currentPhysXVel = ToGlmVec3(dynamic->getLinearVelocity());
        if (glm::distance(rb->velocity, currentPhysXVel) > 0.1f) {
            dynamic->setLinearVelocity(ToPxVec3(rb->velocity));
            dynamic->wakeUp();
        }

        glm::vec3 currentPhysXAngVel = ToGlmVec3(dynamic->getAngularVelocity());
        if (glm::distance(rb->angularVelocity, currentPhysXAngVel) > 0.1f) {
            dynamic->setAngularVelocity(ToPxVec3(rb->angularVelocity));
            dynamic->wakeUp();
        }

        PxTransform physPose = dynamic->getGlobalPose();
        glm::vec3 physPos = ToGlmVec3(physPose.p);

        if (glm::distance(tx->position, physPos) > 1.0f) {
            PxTransform newPose(ToPxVec3(tx->position), ToPxQuat(tx->rotation));
            dynamic->setGlobalPose(newPose);
            dynamic->wakeUp();
        }
    }
}

void Physics3D::FitCollidersToMeshes() {
    // Your original auto-fitting logic
    for (Entity entity : mEntities) {
        if (!engine_->HasComponent(entity, "BoxCollider")) continue;
        if (!engine_->HasComponent(entity, "StaticMesh")) continue;

        auto* box = (BoxCollider*)engine_->GetComponent(entity, "BoxCollider");
        auto* mesh = (StaticMesh*)engine_->GetComponent(entity, "StaticMesh");

        if (!box->HasFittedToMesh && mesh->MeshResource && mesh->MeshResource->cpuMesh) {
            box->FitToMesh(mesh->MeshResource->cpuMesh->aabbMin, mesh->MeshResource->cpuMesh->aabbMax);

            // If the actor already exists in PhysX, we should recreate it or update shape
            // For simplicity, we just remove it so it gets recreated in the next Update loop
            if (mEntityActorMap.count(entity)) {
                mScene->removeActor(*mEntityActorMap[entity]);
                mEntityActorMap[entity]->release();
                mEntityActorMap.erase(entity);
            }
        }
    }
}

// =========================================================================================
//                                  PHYSICS QUERY API
// =========================================================================================

bool Physics3D::RaycastSingle(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
    RaycastHit& outHit, const PhysicsQueryParams& params) {

    PxRaycastBuffer hit;
    bool status = mScene->raycast(ToPxVec3(origin), ToPxVec3(direction), maxDistance, hit);

    if (status) {
        outHit.entity = (Entity)(uintptr_t)hit.block.actor->userData;
        outHit.distance = hit.block.distance;
        outHit.position = ToGlmVec3(hit.block.position);
        outHit.normal = ToGlmVec3(hit.block.normal);
        return true;
    }
    return false;
}

std::vector<RaycastHit> Physics3D::RaycastAll(const glm::vec3& origin, const glm::vec3& direction,
    float maxDistance, const PhysicsQueryParams& params) {

    std::vector<RaycastHit> results;

    // Buffer for up to 32 hits
    const PxU32 bufferSize = 32;
    PxRaycastHit hitBuffer[bufferSize];
    PxRaycastBuffer hit(hitBuffer, bufferSize);

    if (mScene->raycast(ToPxVec3(origin), ToPxVec3(direction), maxDistance, hit)) {
        for (PxU32 i = 0; i < hit.nbTouches; i++) {
            RaycastHit h;
            h.entity = (Entity)(uintptr_t)hitBuffer[i].actor->userData;
            h.distance = hitBuffer[i].distance;
            h.position = ToGlmVec3(hitBuffer[i].position);
            h.normal = ToGlmVec3(hitBuffer[i].normal);
            results.push_back(h);
        }
        // Don't forget the blocking hit if there is one
        if (hit.hasBlock) {
            RaycastHit h;
            h.entity = (Entity)(uintptr_t)hit.block.actor->userData;
            h.distance = hit.block.distance;
            h.position = ToGlmVec3(hit.block.position);
            h.normal = ToGlmVec3(hit.block.normal);
            results.push_back(h);
        }
    }

    // Sort by distance
    std::sort(results.begin(), results.end(), [](const RaycastHit& a, const RaycastHit& b) {
        return a.distance < b.distance;
        });

    return results;
}

bool Physics3D::SphereCastSingle(const glm::vec3& origin, float radius, const glm::vec3& direction,
    float maxDistance, RaycastHit& outHit, const PhysicsQueryParams& params) {

    PxSweepBuffer hit;
    PxSphereGeometry sphere(radius);
    PxTransform pose(ToPxVec3(origin));

    bool status = mScene->sweep(sphere, pose, ToPxVec3(direction), maxDistance, hit);

    if (status) {
        outHit.entity = (Entity)(uintptr_t)hit.block.actor->userData;
        outHit.distance = hit.block.distance;
        outHit.position = ToGlmVec3(hit.block.position);
        outHit.normal = ToGlmVec3(hit.block.normal);
        return true;
    }
    return false;
}

std::vector<RaycastHit> Physics3D::SphereCastAll(const glm::vec3& origin, float radius,
    const glm::vec3& direction, float maxDistance, const PhysicsQueryParams& params) {

    // Similar implementation to RaycastAll but using mScene->sweep
    // Simplified here to just return empty or implement as needed
    std::vector<RaycastHit> results;
    return results;
}

std::vector<Entity> Physics3D::OverlapSphere(const glm::vec3& center, float radius,
    const PhysicsQueryParams& params) {

    std::vector<Entity> entities;

    const PxU32 bufferSize = 64;
    PxOverlapHit hitBuffer[bufferSize];
    PxOverlapBuffer hit(hitBuffer, bufferSize);

    PxSphereGeometry sphere(radius);
    PxTransform pose(ToPxVec3(center));

    if (mScene->overlap(sphere, pose, hit)) {
        for (PxU32 i = 0; i < hit.nbTouches; i++) {
            Entity e = (Entity)(uintptr_t)hitBuffer[i].actor->userData;
            entities.push_back(e);
        }
    }
    return entities;
}

std::vector<Entity> Physics3D::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
    const glm::quat& rotation, const PhysicsQueryParams& params) {

    std::vector<Entity> entities;

    const PxU32 bufferSize = 64;
    PxOverlapHit hitBuffer[bufferSize];
    PxOverlapBuffer hit(hitBuffer, bufferSize);

    PxBoxGeometry box(halfExtents.x, halfExtents.y, halfExtents.z);
    PxTransform pose(ToPxVec3(center), ToPxQuat(rotation));

    if (mScene->overlap(box, pose, hit)) {
        for (PxU32 i = 0; i < hit.nbTouches; i++) {
            Entity e = (Entity)(uintptr_t)hitBuffer[i].actor->userData;
            entities.push_back(e);
        }
    }
    return entities;
}

extern "C" {
    REFLECTION_API System* CreateSystem() { return new Physics3D(); }
}