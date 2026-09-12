#pragma once

#include "vsgthreading/FrameData.h"

#include <random>
#include <vector>
#include <unordered_map>

namespace vsgthreading {

class Simulator
{
public:
    std::shared_ptr<const FrameData> step(double dt, std::vector<AppEvent> events, std::size_t pendingEventCount, std::size_t workerBacklog);

private:
    struct Order
    {
        int id = 0;
        int colorIndex = 0;
        int required = 0;
        int packed = 0;
    };

    void ensureFactoryObjects(FrameData& frame);
    ObjectState makeRobot();
    ObjectState makeBin(int colorIndex);
    ObjectState makeBall(int colorIndex);
    void addRandomBall(FrameData& frame);
    void addObject(ObjectState object, FrameData& frame);
    void removeObject(uint64_t id, FrameData& frame);
    void handleEvent(const AppEvent& event, FrameData& frame);
    void stepRobot(double dt, FrameData& frame);
    void ensureOrders();
    void addOrder(int colorIndex, int required);
    void advanceOrder();
    void updateStats(FrameData& frame) const;
    vsg::dvec3 binPosition(int colorIndex) const;
    vsg::vec4 colorForIndex(int colorIndex) const;
    const ObjectState* nearestNeededBall() const;

    std::unordered_map<uint64_t, ObjectState> objects_;
    uint64_t nextId_ = 1;
    uint64_t robotId_ = 0;
    std::vector<uint64_t> binIds_;
    std::vector<Order> orders_;
    int nextOrderId_ = 1;
    uint64_t simulationFrame_ = 0;
    double simulationTimeSeconds_ = 0.0;
    double spawnAccumulator_ = 0.0;
    double spawnRate_ = 0.7;
    double robotSpeedLimit_ = 1.0;
    double sensorNoise_ = 0.0;
    double commsDropout_ = 0.0;
    double jamRate_ = 0.0;
    double robotBattery_ = 100.0;
    uint64_t packedCount_ = 0;
    uint64_t missedPickups_ = 0;
    bool robotAutoMode_ = true;
    bool robotCarrying_ = false;
    bool robotCharging_ = false;
    bool robotFaulted_ = false;
    int carriedColor_ = 0;
    int robotMode_ = 0;
    bool paused_ = false;
    std::mt19937 rng_{0x76543210u};
};

} // namespace vsgthreading
