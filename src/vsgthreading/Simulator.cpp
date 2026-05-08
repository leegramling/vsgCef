#include "vsgthreading/Simulator.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace vsgthreading {
namespace {
constexpr double kBounds = 8.0;
constexpr double kCubeLifetime = 24.0;
constexpr double kSphereLifetime = 18.0;
constexpr uint64_t kMaxObjects = 100;

double length2(const vsg::dvec3& v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

vsg::dvec3 clampToFloor(vsg::dvec3 p)
{
    p.x = std::clamp(p.x, -kBounds, kBounds);
    p.y = std::clamp(p.y, -kBounds, kBounds);
    p.z = std::max(0.35, p.z);
    return p;
}
} // namespace

std::shared_ptr<const FrameData> Simulator::step(double dt, std::vector<AppEvent> events, std::size_t pendingEventCount, std::size_t workerBacklog)
{
    auto frame = std::make_shared<FrameData>();
    frame->simulationFrame = ++simulationFrame_;
    frame->simulationDeltaSeconds = dt;
    frame->simulationFps = dt > 0.0 ? 1.0 / dt : 0.0;
    frame->pendingAppEvents = pendingEventCount;
    frame->workerBacklog = workerBacklog;

    for (const auto& event : events) handleEvent(event, *frame);

    if (!paused_)
    {
        simulationTimeSeconds_ += dt;
        spawnAccumulator_ += spawnRate_ * dt;
        while (spawnAccumulator_ >= 1.0 && objects_.size() < kMaxObjects)
        {
            addRandomObject(*frame);
            spawnAccumulator_ -= 1.0;
        }

        integrate(dt, *frame);
        collide(*frame);
    }

    frame->simulationTimeSeconds = simulationTimeSeconds_;
    updateStats(*frame);
    frame->createdThisFrame = frame->createdObjects.size();
    frame->updatedThisFrame = frame->updatedObjects.size();
    frame->removedThisFrame = frame->removedObjectIds.size();
    return frame;
}

ObjectState Simulator::makeCube(vsg::dvec3 position)
{
    ObjectState object;
    object.id = nextId_++;
    object.type = ObjectType::Cube;
    object.position = clampToFloor(position);
    object.position.z = 0.35;
    object.scale = {0.7, 0.7, 0.7};
    object.radius = 0.48;
    object.color = vsg::vec4(0.25f, 0.68f, 0.94f, 1.0f);
    return object;
}

ObjectState Simulator::makeSphere()
{
    std::uniform_real_distribution<double> pos(-kBounds * 0.8, kBounds * 0.8);
    std::uniform_real_distribution<double> vel(-3.5, 3.5);

    ObjectState object;
    object.id = nextId_++;
    object.type = ObjectType::Sphere;
    object.position = {pos(rng_), pos(rng_), 0.55};
    object.velocity = {vel(rng_), vel(rng_), 0.0};
    if (length2(object.velocity) < 1.0) object.velocity.x += 2.0;
    object.scale = {0.55, 0.55, 0.55};
    object.radius = 0.55;
    object.color = vsg::vec4(0.96f, 0.48f, 0.24f, 1.0f);
    return object;
}

void Simulator::addRandomObject(FrameData& frame)
{
    std::uniform_int_distribution<int> type(0, 2);
    if (type(rng_) == 0)
    {
        std::uniform_real_distribution<double> pos(-kBounds * 0.85, kBounds * 0.85);
        addObject(makeCube({pos(rng_), pos(rng_), 0.35}), frame);
    }
    else
    {
        addObject(makeSphere(), frame);
    }
}

void Simulator::addObject(ObjectState object, FrameData& frame)
{
    frame.createdObjects.push_back(object);
    objects_[object.id] = std::move(object);
}

void Simulator::removeObject(uint64_t id, FrameData& frame)
{
    if (objects_.erase(id) != 0) frame.removedObjectIds.push_back(id);
}

void Simulator::handleEvent(const AppEvent& event, FrameData& frame)
{
    std::visit([this, &frame](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SpawnBurstEvent>)
        {
            for (uint32_t i = 0; i < value.count && objects_.size() < kMaxObjects; ++i) addRandomObject(frame);
        }
        else if constexpr (std::is_same_v<T, SetPausedEvent>)
        {
            paused_ = value.paused;
        }
        else if constexpr (std::is_same_v<T, SetSpawnRateEvent>)
        {
            spawnRate_ = std::clamp(value.objectsPerSecond, 0.0, 20.0);
        }
        else if constexpr (std::is_same_v<T, ClearObjectsEvent>)
        {
            std::vector<uint64_t> ids;
            ids.reserve(objects_.size());
            for (const auto& [id, object] : objects_) ids.push_back(id);
            for (uint64_t id : ids) removeObject(id, frame);
        }
    }, event);
}

void Simulator::integrate(double dt, FrameData& frame)
{
    std::vector<uint64_t> expired;
    for (auto& [id, object] : objects_)
    {
        object.ageSeconds += dt;
        object.position += object.velocity * dt;

        if (object.type == ObjectType::Sphere)
        {
            for (int axis = 0; axis < 2; ++axis)
            {
                double& p = axis == 0 ? object.position.x : object.position.y;
                double& v = axis == 0 ? object.velocity.x : object.velocity.y;
                if (p < -kBounds || p > kBounds)
                {
                    p = std::clamp(p, -kBounds, kBounds);
                    v *= -0.92;
                }
            }
        }
        else
        {
            object.velocity *= std::pow(0.12, dt);
        }

        const double lifetime = object.type == ObjectType::Sphere ? kSphereLifetime : kCubeLifetime;
        if (object.ageSeconds > lifetime)
            expired.push_back(id);
        else
            frame.updatedObjects.push_back(object);
    }

    for (uint64_t id : expired) removeObject(id, frame);
}

void Simulator::collide(FrameData& frame)
{
    std::vector<uint64_t> spheres;
    std::vector<uint64_t> cubes;
    for (const auto& [id, object] : objects_)
    {
        if (object.type == ObjectType::Sphere)
            spheres.push_back(id);
        else
            cubes.push_back(id);
    }

    for (uint64_t sphereId : spheres)
    {
        auto sphereIt = objects_.find(sphereId);
        if (sphereIt == objects_.end()) continue;
        auto& sphere = sphereIt->second;

        for (uint64_t cubeId : cubes)
        {
            auto cubeIt = objects_.find(cubeId);
            if (cubeIt == objects_.end()) continue;
            auto& cube = cubeIt->second;

            vsg::dvec3 delta = sphere.position - cube.position;
            delta.z = 0.0;
            const double minDistance = sphere.radius + cube.radius;
            const double distSq = length2(delta);
            if (distSq <= 0.0001 || distSq > minDistance * minDistance) continue;

            vsg::dvec3 normal = delta / std::sqrt(distSq);
            const double speedIntoCube = std::max(0.0, -vsg::dot(sphere.velocity, normal));
            cube.velocity -= normal * (speedIntoCube * 0.55 + 0.6);
            sphere.velocity = sphere.velocity - normal * (2.0 * vsg::dot(sphere.velocity, normal));
            sphere.position = cube.position + normal * minDistance;
            sphere.position.z = 0.55;
            ++frame.collisionCount;
            frame.updatedObjects.push_back(cube);
            frame.updatedObjects.push_back(sphere);
        }
    }
}

void Simulator::updateStats(FrameData& frame) const
{
    frame.totalObjects = objects_.size();
    for (const auto& [id, object] : objects_)
    {
        (void)id;
        if (object.type == ObjectType::Cube)
            ++frame.cubeCount;
        else
            ++frame.sphereCount;
    }
}

} // namespace vsgthreading
