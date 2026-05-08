#include "vsgthreading/FrameData.h"

#include <utility>

namespace vsgthreading {

void AppData::publishEvent(AppEvent event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    simulatorEvents_.push_back(std::move(event));
}

std::vector<AppEvent> AppData::takeSimulatorEvents()
{
    std::vector<AppEvent> events;
    std::lock_guard<std::mutex> lock(mutex_);
    events.swap(simulatorEvents_);
    return events;
}

void AppData::publishFrame(std::shared_ptr<const FrameData> frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    latestFrame_ = std::move(frame);
    pendingFrame_ = latestFrame_;
}

std::shared_ptr<const FrameData> AppData::latestFrame() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latestFrame_;
}

std::shared_ptr<const FrameData> AppData::takePendingFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto frame = pendingFrame_;
    pendingFrame_.reset();
    return frame;
}

std::size_t AppData::pendingEventCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return simulatorEvents_.size();
}

void AppData::setWorkerBacklog(std::size_t backlog)
{
    std::lock_guard<std::mutex> lock(mutex_);
    workerBacklog_ = backlog;
}

std::size_t AppData::workerBacklog() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return workerBacklog_;
}

} // namespace vsgthreading
