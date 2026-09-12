#include "vsgthreading/Simulator.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace vsgthreading {
namespace {
constexpr double kBounds = 8.0;
constexpr uint64_t kMaxBalls = 48;
constexpr uint64_t kRobotId = 1;
constexpr double kPickupRadius = 0.65;
constexpr double kDropRadius = 0.75;

double length2(const vsg::dvec3& v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

double distance2XY(const vsg::dvec3& a, const vsg::dvec3& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

vsg::dvec3 moveToward(vsg::dvec3 current, const vsg::dvec3& target, double maxDistance)
{
    vsg::dvec3 delta = target - current;
    delta.z = 0.0;
    const double d2 = length2(delta);
    if (d2 <= 0.000001) return current;

    const double distance = std::sqrt(d2);
    const double step = std::min(distance, maxDistance);
    current += delta * (step / distance);
    return current;
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

    ensureFactoryObjects(*frame);
    ensureOrders();

    for (const auto& event : events) handleEvent(event, *frame);

    if (!paused_)
    {
        simulationTimeSeconds_ += dt;
        spawnAccumulator_ += spawnRate_ * dt;
        while (spawnAccumulator_ >= 1.0)
        {
            addRandomBall(*frame);
            spawnAccumulator_ -= 1.0;
        }

        stepRobot(dt, *frame);
    }

    frame->simulationTimeSeconds = simulationTimeSeconds_;
    updateStats(*frame);
    frame->createdThisFrame = frame->createdObjects.size();
    frame->updatedThisFrame = frame->updatedObjects.size();
    frame->removedThisFrame = frame->removedObjectIds.size();
    return frame;
}

void Simulator::ensureFactoryObjects(FrameData& frame)
{
    if (robotId_ == 0)
    {
        robotId_ = kRobotId;
        nextId_ = std::max(nextId_, robotId_ + 1);
        addObject(makeRobot(), frame);
    }

    while (binIds_.size() < 3)
    {
        const int colorIndex = static_cast<int>(binIds_.size());
        auto bin = makeBin(colorIndex);
        binIds_.push_back(bin.id);
        addObject(bin, frame);
    }
}

ObjectState Simulator::makeRobot()
{
    ObjectState object;
    object.id = robotId_;
    object.type = ObjectType::Robot;
    object.position = {-3.0, -1.8, 0.125};
    object.scale = {1.0, 1.0, 0.25};
    object.radius = 0.7;
    object.color = vsg::vec4(0.98f, 0.72f, 0.16f, 1.0f);
    return object;
}

ObjectState Simulator::makeBin(int colorIndex)
{
    ObjectState object;
    object.id = nextId_++;
    object.type = ObjectType::Bin;
    object.position = binPosition(colorIndex);
    object.scale = {1.8, 1.2, 0.22};
    object.radius = 0.9;
    object.color = colorForIndex(colorIndex) * 0.65f + vsg::vec4(0.08f, 0.08f, 0.08f, 0.35f);
    object.color.a = 1.0f;
    object.colorIndex = colorIndex;
    return object;
}

ObjectState Simulator::makeBall(int colorIndex)
{
    std::uniform_real_distribution<double> lane(-2.4, 2.4);

    ObjectState object;
    object.id = nextId_++;
    object.type = ObjectType::Ball;
    object.position = {-4.8, lane(rng_), 0.45};
    object.scale = {0.45, 0.45, 0.45};
    object.radius = 0.45;
    object.color = colorForIndex(colorIndex);
    object.colorIndex = colorIndex;
    return object;
}

void Simulator::addRandomBall(FrameData& frame)
{
    const uint64_t ballCount = std::count_if(objects_.begin(), objects_.end(), [](const auto& item) {
        return item.second.type == ObjectType::Ball;
    });
    if (ballCount >= kMaxBalls) return;

    std::uniform_int_distribution<int> color(0, 2);
    addObject(makeBall(color(rng_)), frame);
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
            for (uint32_t i = 0; i < value.count; ++i) addRandomBall(frame);
        }
        else if constexpr (std::is_same_v<T, SetPausedEvent>)
        {
            paused_ = value.paused;
        }
        else if constexpr (std::is_same_v<T, SetSpawnRateEvent>)
        {
            spawnRate_ = std::clamp(value.objectsPerSecond, 0.0, 8.0);
        }
        else if constexpr (std::is_same_v<T, ClearObjectsEvent>)
        {
            std::vector<uint64_t> ids;
            for (const auto& [id, object] : objects_)
            {
                if (object.type == ObjectType::Ball) ids.push_back(id);
            }
            for (uint64_t id : ids) removeObject(id, frame);
            robotCarrying_ = false;
        }
        else if constexpr (std::is_same_v<T, SetRobotSpeedEvent>)
        {
            robotSpeedLimit_ = std::clamp(value.speedLimit, 0.25, 2.5);
        }
        else if constexpr (std::is_same_v<T, SetRobotAutoEvent>)
        {
            robotAutoMode_ = value.enabled;
        }
        else if constexpr (std::is_same_v<T, SendRobotChargeEvent>)
        {
            robotCharging_ = true;
            robotFaulted_ = false;
        }
        else if constexpr (std::is_same_v<T, ResetRobotFaultEvent>)
        {
            robotFaulted_ = false;
            robotCharging_ = false;
        }
        else if constexpr (std::is_same_v<T, AddRushOrderEvent>)
        {
            std::uniform_int_distribution<int> color(0, 2);
            addOrder(color(rng_), 3);
        }
        else if constexpr (std::is_same_v<T, SetSensorNoiseEvent>)
        {
            sensorNoise_ = std::clamp(value.value, 0.0, 1.0);
        }
        else if constexpr (std::is_same_v<T, SetCommsDropoutEvent>)
        {
            commsDropout_ = std::clamp(value.value, 0.0, 1.0);
        }
        else if constexpr (std::is_same_v<T, SetJamRateEvent>)
        {
            jamRate_ = std::clamp(value.value, 0.0, 1.0);
        }
    }, event);
}

void Simulator::stepRobot(double dt, FrameData& frame)
{
    auto robotIt = objects_.find(robotId_);
    if (robotIt == objects_.end()) return;

    auto& robot = robotIt->second;
    robotBattery_ = std::clamp(robotBattery_ - dt * (robotCharging_ ? -7.0 : 0.45 * robotSpeedLimit_), 0.0, 100.0);
    if (robotBattery_ <= 2.0) robotFaulted_ = true;
    if (robotBattery_ >= 99.0) robotCharging_ = false;

    if (!robotAutoMode_) robotMode_ = 0;
    if (robotFaulted_) robotMode_ = 5;

    vsg::dvec3 target = robot.position;
    if (robotCharging_)
    {
        robotMode_ = 4;
        target = {-3.0, -1.8, 0.125};
    }
    else if (robotAutoMode_ && !robotFaulted_ && !orders_.empty())
    {
        if (robotCarrying_)
        {
            robotMode_ = 3;
            target = binPosition(carriedColor_);
        }
        else if (const auto* ball = nearestNeededBall())
        {
            robotMode_ = 1;
            target = ball->position;
        }
        else
        {
            robotMode_ = 0;
            target = {-3.8, 0.0, 0.125};
        }
    }

    const double sensorFactor = std::clamp(1.0 - sensorNoise_ * 0.45, 0.2, 1.0);
    const double speed = 3.0 * robotSpeedLimit_ * sensorFactor * (robotBattery_ < 20.0 ? 0.65 : 1.0);
    robot.position = moveToward(robot.position, target, speed * dt);
    robot.position.x = std::clamp(robot.position.x, -kBounds, kBounds);
    robot.position.y = std::clamp(robot.position.y, -kBounds, kBounds);

    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (!robotCarrying_ && robotAutoMode_ && !robotFaulted_ && !orders_.empty())
    {
        const auto* ball = nearestNeededBall();
        if (ball && distance2XY(robot.position, ball->position) < kPickupRadius * kPickupRadius)
        {
            if (chance(rng_) < jamRate_ * 0.35 + sensorNoise_ * 0.2)
            {
                ++missedPickups_;
            }
            else
            {
                carriedColor_ = ball->colorIndex;
                robotCarrying_ = true;
                removeObject(ball->id, frame);
                robotMode_ = 2;
            }
        }
    }

    if (robotCarrying_ && distance2XY(robot.position, binPosition(carriedColor_)) < kDropRadius * kDropRadius)
    {
        robotCarrying_ = false;
        if (!orders_.empty())
        {
            ++orders_.front().packed;
            ++packedCount_;
            if (orders_.front().packed >= orders_.front().required) advanceOrder();
        }
    }

    frame.updatedObjects.push_back(robot);
}

void Simulator::ensureOrders()
{
    while (orders_.size() < 3)
    {
        const int colorIndex = (nextOrderId_ - 1) % 3;
        addOrder(colorIndex, 4 + (nextOrderId_ % 3));
    }
}

void Simulator::addOrder(int colorIndex, int required)
{
    orders_.push_back(Order{nextOrderId_++, colorIndex, required, 0});
}

void Simulator::advanceOrder()
{
    if (!orders_.empty()) orders_.erase(orders_.begin());
    ensureOrders();
}

void Simulator::updateStats(FrameData& frame) const
{
    frame.totalObjects = objects_.size();
    frame.packedCount = packedCount_;
    frame.missedPickups = missedPickups_;
    frame.orderBacklog = orders_.size();
    frame.robotBattery = robotBattery_;
    frame.robotSpeedLimit = robotSpeedLimit_;
    frame.sensorNoise = sensorNoise_;
    frame.commsDropout = commsDropout_;
    frame.jamRate = jamRate_;
    frame.sensorHealth = std::max(0.0, 100.0 - sensorNoise_ * 65.0 - jamRate_ * 20.0);
    frame.commsHealth = std::max(0.0, 100.0 - commsDropout_ * 100.0);
    frame.robotAutoMode = robotAutoMode_;
    frame.robotCarrying = robotCarrying_;
    frame.robotCharging = robotCharging_;
    frame.robotFaulted = robotFaulted_;
    frame.robotMode = robotMode_;

    if (!orders_.empty())
    {
        frame.currentOrderId = orders_[0].id;
        frame.currentOrderColor = orders_[0].colorIndex;
        frame.currentOrderRequired = orders_[0].required;
        frame.currentOrderPacked = orders_[0].packed;
    }
    if (orders_.size() > 1)
    {
        frame.nextOrderId = orders_[1].id;
        frame.nextOrderColor = orders_[1].colorIndex;
        frame.nextOrderRequired = orders_[1].required;
    }

    for (const auto& [id, object] : objects_)
    {
        (void)id;
        if (object.type == ObjectType::Ball)
            ++frame.sphereCount;
        else if (object.type == ObjectType::Robot || object.type == ObjectType::Bin)
            ++frame.cubeCount;
    }
}

vsg::dvec3 Simulator::binPosition(int colorIndex) const
{
    switch (colorIndex)
    {
    case 1: return {4.8, 0.0, 0.18};
    case 2: return {4.8, 2.7, 0.18};
    case 0:
    default: return {4.8, -2.7, 0.18};
    }
}

vsg::vec4 Simulator::colorForIndex(int colorIndex) const
{
    switch (colorIndex)
    {
    case 1: return {0.25f, 0.50f, 0.95f, 1.0f};
    case 2: return {0.25f, 0.78f, 0.32f, 1.0f};
    case 0:
    default: return {0.92f, 0.24f, 0.22f, 1.0f};
    }
}

const ObjectState* Simulator::nearestNeededBall() const
{
    const auto robotIt = objects_.find(robotId_);
    if (robotIt == objects_.end() || orders_.empty()) return nullptr;

    const int neededColor = orders_.front().colorIndex;
    const ObjectState* best = nullptr;
    double bestDistance = std::numeric_limits<double>::max();
    for (const auto& [id, object] : objects_)
    {
        (void)id;
        if (object.type != ObjectType::Ball || object.colorIndex != neededColor) continue;
        const double d2 = distance2XY(robotIt->second.position, object.position);
        if (d2 < bestDistance)
        {
            bestDistance = d2;
            best = &object;
        }
    }
    return best;
}

} // namespace vsgthreading
