#pragma once

#include "vsgthreading/FrameData.h"

#include <memory>

namespace vsgthreading {

class StatsUi
{
public:
    explicit StatsUi(std::shared_ptr<AppData> appData);
    void init();
    void render(const FrameData& frameData);

private:
    std::shared_ptr<AppData> appData_;
    bool paused_ = false;
    float spawnRate_ = 1.5f;
};

} // namespace vsgthreading
