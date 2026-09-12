#pragma once

#include "vsgcef/CefUi.h"

#include <memory>
#include <string>

#include <vsg/app/Viewer.h>
#include <vsgImGui/Texture.h>
#include <vsgImGui/imgui.h>

namespace htmlui {

class HtmlPanel
{
public:
    HtmlPanel(std::string title, std::string inputId, vsgcef::CefSurfaceId surfaceId);

    void renderImGui(const std::shared_ptr<vsgcef::CefUi>& cefUi,
                     vsg::observer_ptr<vsg::Viewer> viewer,
                     uint32_t deviceID,
                     const ImVec2& position,
                     const ImVec2& size,
                     ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings);

    bool focused() const { return focused_; }
    void setFocused(bool focused) { focused_ = focused; }

private:
    struct CefTexture
    {
        vsg::ref_ptr<vsg::ubvec4Array2D> imageData;
        vsg::ref_ptr<vsgImGui::Texture> texture;
        uint64_t paintCount = 0;
        bool compiled = false;
    };

    static uint32_t cefInputModifiers();

    void updateTexture(const vsgcef::CefSurfaceFrame& frame);
    ImTextureID textureId(vsg::observer_ptr<vsg::Viewer> viewer, uint32_t deviceID);
    void forwardInput(const std::shared_ptr<vsgcef::CefUi>& cefUi,
                      const vsgcef::CefSurfaceFrame& frame,
                      const ImVec2& surfaceMin,
                      const ImVec2& surfaceSize);

    std::string title_;
    std::string inputId_;
    vsgcef::CefSurfaceId surfaceId_ = vsgcef::CefSurfaceId::Primary;
    CefTexture texture_;
    bool focused_ = false;
};

} // namespace htmlui
