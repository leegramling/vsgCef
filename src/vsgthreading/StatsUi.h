#pragma once

#include "vsgthreading/FrameData.h"
#include "vsgcef/CefUi.h"

#include <array>
#include <limits>
#include <memory>
#include <vector>

#include <vsg/app/Viewer.h>
#include <vsgImGui/Texture.h>

namespace vsg {
class Viewer;
}

namespace vsgthreading {

class StatsUi
{
public:
    explicit StatsUi(std::shared_ptr<AppData> appData,
                     std::shared_ptr<vsgcef::CefUi> cefUi = {},
                     vsg::observer_ptr<vsg::Viewer> viewer = {});
    void init();
    void render(const FrameData& frameData, uint32_t deviceID = 0);

private:
    struct TypeControl
    {
        bool enabled = true;
        const char* label = "";
        const char* targetBin = "";
        float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
        float spawnWeight = 25.0f;
        float speedBias = 1.0f;
    };

    struct CefTexture
    {
        vsg::ref_ptr<vsg::ubvec4Array2D> imageData;
        vsg::ref_ptr<vsgImGui::Texture> texture;
        uint64_t paintCount = 0;
        bool compiled = false;
    };

    void updateCefTexture(CefTexture& texture, const vsgcef::CefSurfaceFrame& frame);
    ImTextureID cefTextureId(CefTexture& texture, uint32_t deviceID);
    uint32_t currentCefInputModifiers() const;
    void publishFrameDataToCef(const FrameData& frameData);

    std::shared_ptr<AppData> appData_;
    std::shared_ptr<vsgcef::CefUi> cefUi_;
    vsg::observer_ptr<vsg::Viewer> viewer_;
    vsgcef::CefSurfaceId focusedCefSurface_ = vsgcef::CefSurfaceId::Stats;
    vsgcef::CefSurfaceId activeMouseCefSurface_ = vsgcef::CefSurfaceId::Stats;
    bool hasActiveMouseCefSurface_ = false;
    uint64_t lastPublishedSimulationFrame_ = std::numeric_limits<uint64_t>::max();
    double lastPublishedRenderFps_ = -1.0;
    CefTexture statsCefTexture_;
    CefTexture sortingCefTexture_;
    bool paused_ = false;
    float spawnRate_ = 1.5f;
    char mockRateText_[16] = "1.5";
    int mockMaxObjects_ = 100;
    float mockConveyorSpeed_ = 3.0f;
    float mockSortingStrength_ = 0.65f;
    float mockFriction_ = 0.35f;
    float mockRandomness_ = 0.25f;
    float mockCubeMix_ = 45.0f;
    std::array<TypeControl, 4> mockTypes_;
};

} // namespace vsgthreading
