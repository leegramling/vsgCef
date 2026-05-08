#pragma once

#include "vsgthreading/FrameData.h"

#include <random>
#include <unordered_map>

namespace vsgthreading {

class Simulator
{
public:
    std::shared_ptr<const FrameData> step(double dt, std::vector<AppEvent> events, std::size_t pendingEventCount, std::size_t workerBacklog);

private:
    ObjectState makeCube(vsg::dvec3 position);
    ObjectState makeSphere();
    void addRandomObject(FrameData& frame);
    void addObject(ObjectState object, FrameData& frame);
    void removeObject(uint64_t id, FrameData& frame);
    void handleEvent(const AppEvent& event, FrameData& frame);
    void integrate(double dt, FrameData& frame);
    void collide(FrameData& frame);
    void updateStats(FrameData& frame) const;

    std::unordered_map<uint64_t, ObjectState> objects_;
    uint64_t nextId_ = 1;
    uint64_t simulationFrame_ = 0;
    double simulationTimeSeconds_ = 0.0;
    double spawnAccumulator_ = 0.0;
    double spawnRate_ = 1.5;
    bool paused_ = false;
    std::mt19937 rng_{0x76543210u};
};

} // namespace vsgthreading
