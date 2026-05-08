#pragma once

#include <vsg/all.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <variant>
#include <vector>

namespace vsgthreading {

enum class ObjectType
{
    Cube,
    Sphere
};

struct ObjectState
{
    uint64_t id = 0;
    ObjectType type = ObjectType::Cube;
    vsg::dvec3 position{};
    vsg::dvec3 velocity{};
    vsg::dvec3 scale{1.0, 1.0, 1.0};
    vsg::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    double radius = 0.5;
    double ageSeconds = 0.0;
};

struct FrameData
{
    uint64_t simulationFrame = 0;
    double simulationTimeSeconds = 0.0;
    double simulationDeltaSeconds = 0.0;
    double simulationFps = 0.0;
    double renderFps = 0.0;
    uint64_t totalObjects = 0;
    uint64_t cubeCount = 0;
    uint64_t sphereCount = 0;
    uint64_t collisionCount = 0;
    uint64_t createdThisFrame = 0;
    uint64_t updatedThisFrame = 0;
    uint64_t removedThisFrame = 0;
    std::size_t pendingAppEvents = 0;
    std::size_t workerBacklog = 0;
    std::vector<ObjectState> createdObjects;
    std::vector<ObjectState> updatedObjects;
    std::vector<uint64_t> removedObjectIds;
};

struct SpawnBurstEvent
{
    uint32_t count = 1;
};

struct SetPausedEvent
{
    bool paused = false;
};

struct SetSpawnRateEvent
{
    double objectsPerSecond = 1.0;
};

struct ClearObjectsEvent
{
};

using AppEvent = std::variant<SpawnBurstEvent, SetPausedEvent, SetSpawnRateEvent, ClearObjectsEvent>;

class AppData
{
public:
    void publishEvent(AppEvent event);
    std::vector<AppEvent> takeSimulatorEvents();

    void publishFrame(std::shared_ptr<const FrameData> frame);
    std::shared_ptr<const FrameData> latestFrame() const;
    std::shared_ptr<const FrameData> takePendingFrame();

    std::size_t pendingEventCount() const;
    void setWorkerBacklog(std::size_t backlog);
    std::size_t workerBacklog() const;

private:
    mutable std::mutex mutex_;
    std::vector<AppEvent> simulatorEvents_;
    std::shared_ptr<const FrameData> latestFrame_;
    std::shared_ptr<const FrameData> pendingFrame_;
    std::size_t workerBacklog_ = 0;
};

} // namespace vsgthreading
