#include "Physics3D.h"
#include "StaticMeshData.h"
#include "Components/Transform.h"
#include "Components/RigidBody.h"
#include "Components/BoxCollider.h"
#include "Components/StaticMesh.h"
#include <iostream>
#include <algorithm>
#include <MeshCollider.h>
#include <cmath>

class UserErrorCallback : public PxErrorCallback {
public:
    virtual void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override {
        std::cerr << "[PhysX Error] " << message << std::endl;
    }
} gErrorCallback;

static PxDefaultAllocator gAllocator;

static PxFilterFlags ContactReportFilterShader(
    PxFilterObjectAttributes attributes0, PxFilterData filterData0,
    PxFilterObjectAttributes attributes1, PxFilterData filterData1,
    PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
{
    if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
    {
        pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
        return PxFilterFlag::eDEFAULT;
    }

    pairFlags = PxPairFlag::eCONTACT_DEFAULT
        | PxPairFlag::eNOTIFY_TOUCH_FOUND
        | PxPairFlag::eNOTIFY_TOUCH_LOST
        | PxPairFlag::eNOTIFY_CONTACT_POINTS;

    pairFlags |= PxPairFlag::eDETECT_CCD_CONTACT;

    return PxFilterFlag::eDEFAULT;
}

// ============================================================================

void Physics3D::OnInit() {
    InitPhysX();
}

void Physics3D::InitPhysX() {
    mFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    if (!mFoundation) {
        std::cerr << "PxCreateFoundation failed!" << std::endl;
        return;
    }

    mPvd = PxCreatePvd(*mFoundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    mPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);

    mPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *mFoundation, PxTolerancesScale(), true, mPvd);

    if (!PxInitExtensions(*mPhysics, nullptr)) {
        std::cerr << "PxInitExtensions failed!" << std::endl;
    }

    mDispatcher = PxDefaultCpuDispatcherCreate(2);
    mDefaultMaterial = mPhysics->createMaterial(0.5f, 0.5f, 0.0f);

    std::cout << "PhysX Core Initialized Successfully." << std::endl;
}

void Physics3D::Cleanup() {
    if (mScene) {
        mScene->release();
        mScene = nullptr;
    }

    mEntityActorMap.clear();

    PX_RELEASE(mDefaultMaterial);
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

void Physics3D::OnBeginSimulation()
{
    if (!mPhysics) return;

    std::cout << "[Physics] Starting Simulation..." << std::endl;

    PxSceneDesc sceneDesc(mPhysics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = mDispatcher;

    // BUG FIX #1 continued: use our notification-aware shader
    sceneDesc.filterShader = ContactReportFilterShader;

    sceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;

    // BUG FIX #2 continued: enable scene-level CCD
    // Without this, fast objects (falling projectiles) skip through thin geometry
    // in a single timestep — the classic "tunnelling" / pass-through problem.
    sceneDesc.flags |= PxSceneFlag::eENABLE_CCD;

    mScene = mPhysics->createScene(sceneDesc);
    if (!mScene) {
        std::cerr << "[Physics] Failed to create Scene!" << std::endl;
        return;
    }

    mScene->setSimulationEventCallback(this);

    for (Entity entity : mEntities) {
        CreateActorForEntity(entity);
    }
}

void Physics3D::OnEndSimulation()
{
    std::cout << "[Physics] Stopping Simulation..." << std::endl;

    if (mScene) {
        mScene->release();
        mScene = nullptr;
    }

    mEntityActorMap.clear();
    mCollisionEvents.clear();
}

void Physics3D::Update(float dt) {
    if (!mScene) return;

    mCollisionEvents.clear();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > mMaxFrameTime) dt = mMaxFrameTime;

    FitCollidersToMeshes();

    // Handle runtime spawning
    for (Entity entity : mEntities) {
        if (mEntityActorMap.find(entity) == mEntityActorMap.end()) {
            bool hasTransform = engine_->HasComponent(entity, "Transform");
            bool hasBox = engine_->HasComponent(entity, "BoxCollider");
            bool hasMesh = engine_->HasComponent(entity, "MeshCollider");

            if (hasTransform && (hasBox || hasMesh)) {
                CreateActorForEntity(entity);
            }
        }
    }

    // Handle runtime despawning
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

    // Sync ECS -> PhysX (kinematic overrides, teleports, ECS-driven velocities)
    SyncECSToPhysX();

    mAccumulator += dt;
    int subSteps = 0;
    while (mAccumulator >= mFixedTimeStep && subSteps < mMaxSubSteps) {
        mScene->simulate(mFixedTimeStep);
        mScene->fetchResults(true);
        mAccumulator -= mFixedTimeStep;
        subSteps++;
    }
    if (subSteps == mMaxSubSteps) {
        if (mAccumulator > mFixedTimeStep) mAccumulator = mFixedTimeStep;
    }
    if (subSteps == 0) return;

    // Sync PhysX -> ECS for all awake dynamics
    for (auto& pair : mEntityActorMap) {
        Entity entity = pair.first;
        PxRigidActor* actor = pair.second;

        if (actor->getType() == PxActorType::eRIGID_STATIC) continue;

        PxRigidDynamic* dynamic = (PxRigidDynamic*)actor;
        if (dynamic->isSleeping()) continue;

        PxTransform t = dynamic->getGlobalPose();

        Transform* tx = (Transform*)engine_->GetComponentForWrite(entity, "Transform");
        if (tx) {
            tx->position = ToGlmVec3(t.p);
            tx->rotation = ToGlmQuat(t.q);
            engine_->MarkEntityDirty(entity, "Transform");
        }

        RigidBody* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");
        if (rb) {
            rb->velocity = ToGlmVec3(dynamic->getLinearVelocity());
            rb->angularVelocity = ToGlmVec3(dynamic->getAngularVelocity());
        }
    }

    // Broadcast collision/trigger events to game systems
    for (const CollisionEventData& col : mCollisionEvents)
    {
        const char* eventType = col.IsTrigger
            ? (col.HasEnded ? EVENT_TRIGGER_END : EVENT_TRIGGER_BEGIN)
            : (col.HasEnded ? EVENT_COLLISION_END : EVENT_COLLISION_BEGIN);

        Event e(eventType);
        e.SetParam<CollisionEventPayload>("col", { col.EntityA, col.EntityB });
        engine_->SendEvent(e);
    }
}

void Physics3D::CreateActorForEntity(Entity entity) {
    if (!mScene) return;

    auto* tx = (Transform*)engine_->GetComponent(entity, "Transform");
    if (!tx) return;

    bool hasBox = engine_->HasComponent(entity, "BoxCollider");
    bool hasMeshCol = engine_->HasComponent(entity, "MeshCollider");
    auto* rb = (RigidBody*)engine_->GetComponent(entity, "RigidBody");

    if (!hasBox && !hasMeshCol) return;

    // Sanitize rotation quaternion — a zero quaternion will crash PhysX
    glm::quat q = tx->rotation;
    if (glm::length(q) < 0.0001f)
        q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    else
        q = glm::normalize(q);

    PxTransform pxTransform(ToPxVec3(tx->position), ToPxQuat(q));
    PxRigidActor* actor = nullptr;

    // ------------------------------------------------------------------
    // Mesh Collider (always static — triangle meshes can't be dynamic in PhysX)
    // ------------------------------------------------------------------
    if (hasMeshCol) {
        auto* meshCol = (MeshCollider*)engine_->GetComponent(entity, "MeshCollider");
        PxRigidStatic* staticActor = mPhysics->createRigidStatic(pxTransform);

        if (meshCol->meshData && !meshCol->meshData->physicsData.empty()) {
            PxDefaultMemoryInputData inputData(
                meshCol->meshData->physicsData.data(),
                (PxU32)meshCol->meshData->physicsData.size()
            );

            PxTriangleMesh* triMesh = mPhysics->createTriangleMesh(inputData);
            if (triMesh) {
                PxMeshScale meshScale(ToPxVec3(tx->scale), PxQuat(PxIdentity));
                PxTriangleMeshGeometry triGeom(triMesh, meshScale);
                PxShape* shape = mPhysics->createShape(triGeom, *mDefaultMaterial);
                staticActor->attachShape(*shape);
                triMesh->release();
                shape->release();
            }
        }
        actor = staticActor;
    }
    // ------------------------------------------------------------------
    // Box Collider (can be static or dynamic)
    // ------------------------------------------------------------------
    else if (hasBox) {
        auto* box = (BoxCollider*)engine_->GetComponent(entity, "BoxCollider");

        // Ensure half-extents are never zero (PhysX will assert)
        glm::vec3 halfExtents = glm::max((box->size * tx->scale) * 0.5f, glm::vec3(0.01f));

        PxBoxGeometry geometry(halfExtents.x, halfExtents.y, halfExtents.z);

        bool isStatic = (rb == nullptr) || rb->isStatic;

        if (isStatic) {
            actor = mPhysics->createRigidStatic(pxTransform);
        }
        else {
            PxRigidDynamic* dynamic = mPhysics->createRigidDynamic(pxTransform);

            // Use RigidBody's friction/restitution instead of the global default
            PxMaterial* mat = mPhysics->createMaterial(rb->friction, rb->friction, rb->restitution);

            PxRigidBodyExt::updateMassAndInertia(*dynamic, rb->mass);
            dynamic->setLinearVelocity(ToPxVec3(rb->velocity));

            dynamic->setLinearDamping(0.01f);
            dynamic->setAngularDamping(0.05f);

            if (rb->lockAngular) {
                dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, true);
                dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, true);
                dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, true);
            }

            // BUG FIX #3 — useGravity flag was never applied to the PhysX actor
            if (!rb->useGravity)
                dynamic->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);

            // BUG FIX #2 continued — enable CCD on individual dynamic actors
            // The scene flag alone is not enough; each dynamic must opt in.
            dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);

            actor = dynamic;

            mat->release(); // Shape holds its own ref; safe to release here
        }

        if (actor) {
            // BUG FIX #4 — Create material inline with correct values before shape creation
            PxMaterial* shapeMat = (rb && !isStatic)
                ? mPhysics->createMaterial(rb->friction, rb->friction, rb->restitution)
                : mDefaultMaterial;

            PxShape* shape = mPhysics->createShape(geometry, *shapeMat);

            // BUG FIX #5 — BoxCollider::isTrigger was declared without a default value
            // ('bool isTrigger;' = uninitialized garbage memory). This randomly made
            // solid objects behave as triggers (no collision response), causing
            // the "pass-through" symptom. Fixed in BoxCollider.h (= false).
            // This code now reads the correct value.
            shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, !box->isTrigger);
            shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, box->isTrigger);

            PxTransform localOffset(ToPxVec3(box->offset));
            shape->setLocalPose(localOffset);

            actor->attachShape(*shape);
            shape->release();

            if (rb && !isStatic)
                shapeMat->release();
        }
    }

    if (actor) {
        actor->userData = (void*)(uintptr_t)entity;
        mScene->addActor(*actor);
        mEntityActorMap[entity] = actor;
    }
}

void Physics3D::DestroyActorForEntity(Entity entity)
{
    if (!mScene) return;
    auto it = mEntityActorMap.find(entity);
    if (it != mEntityActorMap.end()) {
        mScene->removeActor(*it->second);
        it->second->release();
        mEntityActorMap.erase(it);
    }
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

        // Push ECS velocity to PhysX if game logic changed it (e.g. PlayerController)
        glm::vec3 physXVel = ToGlmVec3(dynamic->getLinearVelocity());
        if (glm::distance(rb->velocity, physXVel) > 0.1f) {
            dynamic->setLinearVelocity(ToPxVec3(rb->velocity));
            dynamic->wakeUp();
        }

        // Teleport if ECS position diverged significantly (scripted teleport etc.)
        glm::vec3 physPos = ToGlmVec3(dynamic->getGlobalPose().p);
        if (glm::distance(tx->position, physPos) > 0.5f) {
            PxTransform newPose(ToPxVec3(tx->position), ToPxQuat(glm::normalize(tx->rotation)));
            dynamic->setGlobalPose(newPose);
            dynamic->setLinearVelocity(PxVec3(0));
            dynamic->setAngularVelocity(PxVec3(0));
            dynamic->wakeUp();
        }
    }
}

void Physics3D::FitCollidersToMeshes() {
    if (!mScene) return;

    for (Entity entity : mEntities) {
        if (!engine_->HasComponent(entity, "BoxCollider")) continue;
        if (!engine_->HasComponent(entity, "StaticMesh"))  continue;

        auto* box = (BoxCollider*)engine_->GetComponent(entity, "BoxCollider");
        auto* mesh = (StaticMesh*)engine_->GetComponent(entity, "StaticMesh");

        if (!box->HasFittedToMesh && mesh->MeshResource && mesh->MeshResource->cpuMesh) {
            box->FitToMesh(
                mesh->MeshResource->cpuMesh->aabbMin,
                mesh->MeshResource->cpuMesh->aabbMax
            );

            if (mEntityActorMap.count(entity)) {
                DestroyActorForEntity(entity);
                CreateActorForEntity(entity);
            }
        }
    }
}

// =============================================================================
//  PHYSICS QUERIES
// =============================================================================

bool Physics3D::RaycastSingle(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
    RaycastHit& outHit, const PhysicsQueryParams& params)
{
    if (!mScene) return false;

    PxRaycastBuffer hit;
    if (mScene->raycast(ToPxVec3(origin), ToPxVec3(glm::normalize(direction)), maxDistance, hit)) {
        outHit.entity = (Entity)(uintptr_t)hit.block.actor->userData;
        outHit.distance = hit.block.distance;
        outHit.position = ToGlmVec3(hit.block.position);
        outHit.normal = ToGlmVec3(hit.block.normal);
        return true;
    }
    return false;
}

std::vector<RaycastHit> Physics3D::RaycastAll(const glm::vec3& origin, const glm::vec3& direction,
    float maxDistance, const PhysicsQueryParams& params)
{
    std::vector<RaycastHit> results;
    if (!mScene) return results;

    const PxU32 bufferSize = 32;
    PxRaycastHit hitBuffer[bufferSize];
    PxRaycastBuffer hit(hitBuffer, bufferSize);

    if (mScene->raycast(ToPxVec3(origin), ToPxVec3(glm::normalize(direction)), maxDistance, hit)) {
        for (PxU32 i = 0; i < hit.nbTouches; i++) {
            RaycastHit h;
            h.entity = (Entity)(uintptr_t)hitBuffer[i].actor->userData;
            h.distance = hitBuffer[i].distance;
            h.position = ToGlmVec3(hitBuffer[i].position);
            h.normal = ToGlmVec3(hitBuffer[i].normal);
            results.push_back(h);
        }
        if (hit.hasBlock) {
            RaycastHit h;
            h.entity = (Entity)(uintptr_t)hit.block.actor->userData;
            h.distance = hit.block.distance;
            h.position = ToGlmVec3(hit.block.position);
            h.normal = ToGlmVec3(hit.block.normal);
            results.push_back(h);
        }
    }

    std::sort(results.begin(), results.end(), [](const RaycastHit& a, const RaycastHit& b) {
        return a.distance < b.distance;
        });

    return results;
}

bool Physics3D::SphereCastSingle(const glm::vec3& origin, float radius, const glm::vec3& direction,
    float maxDistance, RaycastHit& outHit, const PhysicsQueryParams& params)
{
    if (!mScene) return false;

    PxSweepBuffer hit;
    PxSphereGeometry sphere(radius);
    PxTransform pose(ToPxVec3(origin));

    if (mScene->sweep(sphere, pose, ToPxVec3(glm::normalize(direction)), maxDistance, hit)) {
        outHit.entity = (Entity)(uintptr_t)hit.block.actor->userData;
        outHit.distance = hit.block.distance;
        outHit.position = ToGlmVec3(hit.block.position);
        outHit.normal = ToGlmVec3(hit.block.normal);
        return true;
    }
    return false;
}

std::vector<RaycastHit> Physics3D::SphereCastAll(const glm::vec3& origin, float radius,
    const glm::vec3& direction, float maxDistance, const PhysicsQueryParams& params)
{
    // Stub — extend as needed
    return {};
}

std::vector<Entity> Physics3D::OverlapSphere(const glm::vec3& center, float radius,
    const PhysicsQueryParams& params)
{
    std::vector<Entity> entities;
    if (!mScene) return entities;

    const PxU32 bufferSize = 64;
    PxOverlapHit hitBuffer[bufferSize];
    PxOverlapBuffer hit(hitBuffer, bufferSize);

    PxSphereGeometry sphere(radius);
    PxTransform pose(ToPxVec3(center));

    if (mScene->overlap(sphere, pose, hit)) {
        for (PxU32 i = 0; i < hit.nbTouches; i++)
            entities.push_back((Entity)(uintptr_t)hitBuffer[i].actor->userData);
    }
    return entities;
}

std::vector<Entity> Physics3D::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
    const glm::quat& rotation, const PhysicsQueryParams& params)
{
    std::vector<Entity> entities;
    if (!mScene) return entities;

    const PxU32 bufferSize = 64;
    PxOverlapHit hitBuffer[bufferSize];
    PxOverlapBuffer hit(hitBuffer, bufferSize);

    PxBoxGeometry box(halfExtents.x, halfExtents.y, halfExtents.z);
    PxTransform pose(ToPxVec3(center), ToPxQuat(rotation));

    if (mScene->overlap(box, pose, hit)) {
        for (PxU32 i = 0; i < hit.nbTouches; i++)
            entities.push_back((Entity)(uintptr_t)hitBuffer[i].actor->userData);
    }
    return entities;
}

// =============================================================================
//  COLLISION / TRIGGER CALLBACKS
// =============================================================================

// BUG FIX #6 — onContact was completely empty ("// Implement contact logic if needed")
// This is the callback PhysX calls for every solid collision pair.
// Without it populating mCollisionEvents, EVENT_COLLISION_BEGIN was never sent,
// so SurvivalSystem could never detect projectile-player hits.
void Physics3D::onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs)
{
    // Skip pairs where one of the actors has been deleted mid-frame
    if (pairHeader.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 |
        PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
        return;

    Entity entityA = (Entity)(uintptr_t)pairHeader.actors[0]->userData;
    Entity entityB = (Entity)(uintptr_t)pairHeader.actors[1]->userData;

    for (PxU32 i = 0; i < nbPairs; i++)
    {
        const PxContactPair& cp = pairs[i];

        if (cp.flags & (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1))
            continue;

        CollisionEventData event;
        event.EntityA = entityA;
        event.EntityB = entityB;
        event.IsTrigger = false;
        event.HasEnded = (cp.events & PxPairFlag::eNOTIFY_TOUCH_LOST);

        mCollisionEvents.push_back(event);
    }
}

void Physics3D::onTrigger(PxTriggerPair* pairs, PxU32 count)
{
    for (PxU32 i = 0; i < count; i++)
    {
        if (pairs[i].flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER |
            PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
            continue;

        CollisionEventData event;
        event.EntityA = (Entity)(uintptr_t)pairs[i].triggerActor->userData;
        event.EntityB = (Entity)(uintptr_t)pairs[i].otherActor->userData;
        event.IsTrigger = true;
        event.HasEnded = (pairs[i].status == PxPairFlag::eNOTIFY_TOUCH_LOST);

        mCollisionEvents.push_back(event);
    }
}

extern "C" {
    REFLECTION_API System* CreateSystem() { return new Physics3D(); }
}
