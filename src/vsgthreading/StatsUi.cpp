#include "vsgthreading/StatsUi.h"

#include "vsgthreading/Profiling.h"
#include "vsgcef/CefUi.h"

#include <vsgImGui/imgui.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace vsgthreading {
namespace {

void renderCefSurfaceMockup(const char* windowTitle,
                            const char* surfaceId,
                            const char* pageName,
                            const vsgcef::CefSurfaceSnapshot& snapshot,
                            ImTextureID textureId,
                            const ImVec2& defaultPos,
                            const ImVec2& defaultSize,
                            const std::shared_ptr<vsgcef::CefUi>& cefUi,
                            vsgcef::CefSurfaceId cefSurfaceId,
                            vsgcef::CefSurfaceId& focusedSurface,
                            vsgcef::CefSurfaceId& activeMouseSurface,
                            bool& hasActiveMouseSurface,
                            uint32_t modifiers,
                            bool attached = false)
{
    VSGCEF_ZONE("Render CEF ImGui surface");

    ImGui::SetNextWindowPos(defaultPos, attached ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(defaultSize, attached ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    if (attached) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags windowFlags = attached
        ? ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
        : ImGuiWindowFlags_None;
    if (!ImGui::Begin(windowTitle, nullptr, windowFlags))
    {
        if (attached) ImGui::PopStyleVar();
        return;
    }

    ImVec2 surfaceSize = ImGui::GetContentRegionAvail();
    surfaceSize.x = std::max(surfaceSize.x, 1.0f);
    surfaceSize.y = std::max(surfaceSize.y, 1.0f);
    const ImVec2 surfaceMin = ImGui::GetCursorScreenPos();
    if (textureId)
    {
        ImGui::Image(textureId, surfaceSize);
    }
    else
    {
        const ImVec2 surfaceMax(surfaceMin.x + surfaceSize.x, surfaceMin.y + surfaceSize.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(surfaceMin, surfaceMax, IM_COL32(24, 28, 29, 255), 4.0f);
        drawList->AddRect(surfaceMin, surfaceMax, IM_COL32(80, 92, 95, 255), 4.0f);
        drawList->AddText(ImVec2(surfaceMin.x + 12.0f, surfaceMin.y + 12.0f),
                          IM_COL32(220, 228, 230, 255),
                          windowTitle);
        drawList->AddText(ImVec2(surfaceMin.x + 12.0f, surfaceMin.y + 34.0f),
                          IM_COL32(155, 167, 170, 255),
                          pageName);
        drawList->AddText(ImVec2(surfaceMin.x + 12.0f, surfaceMin.y + 56.0f),
                          IM_COL32(155, 167, 170, 255),
                          snapshot.available ? "Waiting for compiled CEF texture." : "CEF runtime unavailable; showing placeholder only.");
    }

    ImGui::SetCursorScreenPos(surfaceMin);

    ImGui::InvisibleButton(surfaceId, surfaceSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = hasActiveMouseSurface && activeMouseSurface == cefSurfaceId;
    const bool receivesMouse = hovered || active;
    const ImVec2 mouse = ImGui::GetMousePos();
    const int localX = std::clamp(static_cast<int>(mouse.x - surfaceMin.x), 0, static_cast<int>(surfaceSize.x) - 1);
    const int localY = std::clamp(static_cast<int>(mouse.y - surfaceMin.y), 0, static_cast<int>(surfaceSize.y) - 1);
    const int browserWidth = snapshot.width > 0 ? snapshot.width : static_cast<int>(surfaceSize.x);
    const int browserHeight = snapshot.height > 0 ? snapshot.height : static_cast<int>(surfaceSize.y);
    const int browserX = std::clamp(static_cast<int>((static_cast<float>(localX) / surfaceSize.x) * static_cast<float>(browserWidth)), 0, browserWidth - 1);
    const int browserY = std::clamp(static_cast<int>((static_cast<float>(localY) / surfaceSize.y) * static_cast<float>(browserHeight)), 0, browserHeight - 1);

    if (receivesMouse && cefUi)
    {
        VSGCEF_ZONE("Forward ImGui mouse input to CEF");
        cefUi->sendMouseMove(cefSurfaceId, browserX, browserY, modifiers);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            focusedSurface = cefSurfaceId;
            activeMouseSurface = cefSurfaceId;
            hasActiveMouseSurface = true;
            cefUi->setFocus(cefSurfaceId, true);
            cefUi->sendMouseClick(cefSurfaceId, browserX, browserY, modifiers, vsgcef::CefMouseButton::Left, false, 1);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            cefUi->sendMouseClick(cefSurfaceId, browserX, browserY, modifiers, vsgcef::CefMouseButton::Left, true, 1);
            if (activeMouseSurface == cefSurfaceId) hasActiveMouseSurface = false;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            focusedSurface = cefSurfaceId;
            activeMouseSurface = cefSurfaceId;
            hasActiveMouseSurface = true;
            cefUi->setFocus(cefSurfaceId, true);
            cefUi->sendMouseClick(cefSurfaceId, browserX, browserY, modifiers, vsgcef::CefMouseButton::Right, false, 1);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            cefUi->sendMouseClick(cefSurfaceId, browserX, browserY, modifiers, vsgcef::CefMouseButton::Right, true, 1);
            if (activeMouseSurface == cefSurfaceId) hasActiveMouseSurface = false;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            focusedSurface = cefSurfaceId;
            activeMouseSurface = cefSurfaceId;
            hasActiveMouseSurface = true;
            cefUi->setFocus(cefSurfaceId, true);
            cefUi->sendMouseClick(cefSurfaceId, browserX, browserY, modifiers, vsgcef::CefMouseButton::Middle, false, 1);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
        {
            cefUi->sendMouseClick(cefSurfaceId, browserX, browserY, modifiers, vsgcef::CefMouseButton::Middle, true, 1);
            if (activeMouseSurface == cefSurfaceId) hasActiveMouseSurface = false;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)
        {
            cefUi->sendMouseWheel(cefSurfaceId,
                                  browserX,
                                  browserY,
                                  modifiers,
                                  static_cast<int>(io.MouseWheelH * 120.0f),
                                  static_cast<int>(io.MouseWheel * 120.0f));
        }
    }

    if (cefUi && focusedSurface == cefSurfaceId)
    {
        VSGCEF_ZONE("Forward ImGui keyboard input to CEF");
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextFrameWantCaptureKeyboard(true);

        struct KeyMap
        {
            ImGuiKey imguiKey;
            int windowsKeyCode;
        };
        constexpr KeyMap keyMap[] = {
            {ImGuiKey_Backspace, 0x08},
            {ImGuiKey_Tab, 0x09},
            {ImGuiKey_Enter, 0x0D},
            {ImGuiKey_Escape, 0x1B},
            {ImGuiKey_LeftArrow, 0x25},
            {ImGuiKey_UpArrow, 0x26},
            {ImGuiKey_RightArrow, 0x27},
            {ImGuiKey_DownArrow, 0x28},
            {ImGuiKey_Delete, 0x2E}};

        for (const auto& key : keyMap)
        {
            if (ImGui::IsKeyPressed(key.imguiKey, false)) cefUi->sendKey(cefSurfaceId, key.windowsKeyCode, modifiers, false);
            if (ImGui::IsKeyReleased(key.imguiKey)) cefUi->sendKey(cefSurfaceId, key.windowsKeyCode, modifiers, true);
        }

        for (auto character : io.InputQueueCharacters)
        {
            if (character == 0) continue;
            const auto codepoint = static_cast<uint32_t>(character);
            cefUi->sendKey(cefSurfaceId, static_cast<int>(codepoint), modifiers, false);
            cefUi->sendKeyChar(cefSurfaceId, codepoint, modifiers);
            cefUi->sendKey(cefSurfaceId, static_cast<int>(codepoint), modifiers, true);
        }
    }

    if (hovered)
    {
        ImGui::SetTooltip("CEF input target: %d, %d", browserX, browserY);
    }

    ImGui::End();
    if (attached) ImGui::PopStyleVar();
}

} // namespace

StatsUi::StatsUi(std::shared_ptr<AppData> appData, std::shared_ptr<vsgcef::CefUi> cefUi, vsg::observer_ptr<vsg::Viewer> viewer) :
    appData_(std::move(appData)),
    cefUi_(std::move(cefUi)),
    viewer_(viewer),
    mockTypes_{
        TypeControl{true, "A", "North", {0.90f, 0.20f, 0.18f, 1.0f}, 25.0f, 1.00f},
        TypeControl{true, "B", "East", {0.20f, 0.45f, 0.95f, 1.0f}, 25.0f, 0.90f},
        TypeControl{true, "C", "South", {0.25f, 0.78f, 0.32f, 1.0f}, 25.0f, 1.10f},
        TypeControl{true, "D", "West", {0.95f, 0.78f, 0.20f, 1.0f}, 25.0f, 1.00f}}
{
}

void StatsUi::updateCefTexture(CefTexture& texture, const vsgcef::CefSurfaceFrame& frame)
{
    VSGCEF_ZONE("StatsUi::updateCefTexture");

    const auto& snapshot = frame.snapshot;
    if (!snapshot.available || snapshot.width <= 0 || snapshot.height <= 0 || frame.bgra.empty()) return;
    if (texture.paintCount == snapshot.paintCount && texture.texture) return;

    const auto width = static_cast<uint32_t>(snapshot.width);
    const auto height = static_cast<uint32_t>(snapshot.height);
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (frame.bgra.size() < expected) return;
    VSGCEF_PLOT("CEF texture bytes", static_cast<int64_t>(expected));

    const bool needsNewTexture = !texture.imageData || texture.imageData->width() != width || texture.imageData->height() != height;
    if (needsNewTexture)
    {
        VSGCEF_ZONE("Allocate CEF VSG texture data");
        texture.imageData = vsg::ubvec4Array2D::create(width, height);
        texture.imageData->properties.format = VK_FORMAT_B8G8R8A8_UNORM;
        texture.imageData->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        texture.texture = {};
        texture.compiled = false;
    }

    auto* dst = reinterpret_cast<uint8_t*>(texture.imageData->dataPointer());
    const auto* src = frame.bgra.data();
    {
        VSGCEF_ZONE("Copy CEF BGRA pixels to VSG texture");
        std::memcpy(dst, src, expected);
    }
    texture.imageData->dirty();
    if (!texture.texture)
    {
        VSGCEF_ZONE("Create vsgImGui texture wrapper");
        texture.texture = vsgImGui::Texture::create(texture.imageData);
    }
    texture.paintCount = snapshot.paintCount;
}

ImTextureID StatsUi::cefTextureId(CefTexture& texture, uint32_t deviceID)
{
    VSGCEF_ZONE("StatsUi::cefTextureId");

    if (!texture.texture) return {};
    if (!texture.compiled)
    {
        auto viewer = vsg::ref_ptr<vsg::Viewer>(viewer_);
        if (!viewer || !viewer->compileManager) return {};

        vsg::CompileResult result;
        {
            VSGCEF_ZONE("Compile CEF ImGui texture");
            result = viewer->compileManager->compile(texture.texture);
        }
        if (result)
        {
            VSGCEF_ZONE("Update viewer with compiled CEF texture");
            updateViewer(*viewer, result);
            texture.compiled = true;
        }
        else
        {
            return {};
        }
    }

    return texture.texture->id(deviceID);
}

uint32_t StatsUi::currentCefInputModifiers() const
{
    const ImGuiIO& io = ImGui::GetIO();
    uint32_t modifiers = vsgcef::CefInputModifierNone;
    if (io.KeyShift) modifiers |= vsgcef::CefInputModifierShift;
    if (io.KeyCtrl) modifiers |= vsgcef::CefInputModifierControl;
    if (io.KeyAlt) modifiers |= vsgcef::CefInputModifierAlt;
    if (io.MouseDown[ImGuiMouseButton_Left]) modifiers |= vsgcef::CefInputModifierLeftMouseButton;
    if (io.MouseDown[ImGuiMouseButton_Middle]) modifiers |= vsgcef::CefInputModifierMiddleMouseButton;
    if (io.MouseDown[ImGuiMouseButton_Right]) modifiers |= vsgcef::CefInputModifierRightMouseButton;
    return modifiers;
}

void StatsUi::publishFrameDataToCef(const FrameData& frameData)
{
    VSGCEF_ZONE("StatsUi::publishFrameDataToCef");

    if (!cefUi_) return;
    if (frameData.simulationFrame == lastPublishedSimulationFrame_ && frameData.renderFps == lastPublishedRenderFps_) return;

    lastPublishedSimulationFrame_ = frameData.simulationFrame;
    lastPublishedRenderFps_ = frameData.renderFps;

    std::ostringstream json;
    json << std::fixed << std::setprecision(3);
    json << "{"
         << "\"renderFps\":" << frameData.renderFps << ","
         << "\"simulationFps\":" << frameData.simulationFps << ","
         << "\"simulationFrame\":" << frameData.simulationFrame << ","
         << "\"totalObjects\":" << frameData.totalObjects << ","
         << "\"cubeCount\":" << frameData.cubeCount << ","
         << "\"sphereCount\":" << frameData.sphereCount << ","
         << "\"createdThisFrame\":" << frameData.createdThisFrame << ","
         << "\"updatedThisFrame\":" << frameData.updatedThisFrame << ","
         << "\"removedThisFrame\":" << frameData.removedThisFrame << ","
         << "\"collisionCount\":" << frameData.collisionCount << ","
         << "\"pendingAppEvents\":" << frameData.pendingAppEvents << ","
         << "\"workerBacklog\":" << frameData.workerBacklog
         << "}";

    const std::string script = "if (window.vsgCef && window.vsgCef.receiveFrameData) window.vsgCef.receiveFrameData(" + json.str() + ");";
    VSGCEF_PLOT("CEF stats JSON bytes", static_cast<int64_t>(script.size()));
    {
        VSGCEF_ZONE("Execute stats frame JavaScript");
        cefUi_->executeJavaScript(vsgcef::CefSurfaceId::Stats, script);
    }
    {
        VSGCEF_ZONE("Execute sorting frame JavaScript");
        cefUi_->executeJavaScript(vsgcef::CefSurfaceId::Sorting, script);
    }
}

void StatsUi::init()
{
}

void StatsUi::render(const FrameData& frameData, uint32_t deviceID)
{
    VSGCEF_ZONE("StatsUi::render");

    publishFrameDataToCef(frameData);

    vsgcef::CefSurfaceFrame statsFrame;
    vsgcef::CefSurfaceFrame sortingFrame;
    if (cefUi_)
    {
        VSGCEF_ZONE("Read CEF surface frames");
        statsFrame = cefUi_->statsFrame();
        sortingFrame = cefUi_->sortingFrame();
    }
    updateCefTexture(statsCefTexture_, statsFrame);
    updateCefTexture(sortingCefTexture_, sortingFrame);

    const uint32_t cefModifiers = currentCefInputModifiers();
    renderCefSurfaceMockup("CEF Stats Panel", "cef_stats_surface_input", "cef_ui/stats.html", statsFrame.snapshot, cefTextureId(statsCefTexture_, deviceID), ImVec2(0.0f, 0.0f), ImVec2(300.0f, 800.0f), cefUi_, vsgcef::CefSurfaceId::Stats, focusedCefSurface_, activeMouseCefSurface_, hasActiveMouseCefSurface_, cefModifiers, true);
    renderCefSurfaceMockup("CEF Sorting Form Panel", "cef_sorting_surface_input", "cef_ui/sorting-form.html", sortingFrame.snapshot, cefTextureId(sortingCefTexture_, deviceID), ImVec2(388.0f, 12.0f), ImVec2(560.0f, 420.0f), cefUi_, vsgcef::CefSurfaceId::Sorting, focusedCefSurface_, activeMouseCefSurface_, hasActiveMouseCefSurface_, cefModifiers);
}

} // namespace vsgthreading
