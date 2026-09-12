#include "htmlui/HtmlPanel.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace htmlui {

HtmlPanel::HtmlPanel(std::string title, std::string inputId, std::string surfaceId) :
    title_(std::move(title)),
    inputId_(std::move(inputId)),
    surfaceId_(surfaceId)
{
}

void HtmlPanel::renderImGui(const std::shared_ptr<vsgcef::CefUi>& cefUi,
                            vsg::observer_ptr<vsg::Viewer> viewer,
                            uint32_t deviceID,
                            const ImVec2& position,
                            const ImVec2& size,
                            ImGuiWindowFlags flags)
{
    if (!cefUi) return;

    const auto frame = cefUi->surfaceFrame(surfaceId_);
    updateTexture(frame);

    ImGui::SetNextWindowPos(position, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(title_.c_str(), nullptr, flags))
    {
        ImVec2 surfaceSize = ImGui::GetContentRegionAvail();
        surfaceSize.x = std::max(surfaceSize.x, 1.0f);
        surfaceSize.y = std::max(surfaceSize.y, 1.0f);
        cefUi->resizeSurface(surfaceId_, static_cast<int>(surfaceSize.x), static_cast<int>(surfaceSize.y));

        const ImVec2 surfaceMin = ImGui::GetCursorScreenPos();
        if (ImTextureID id = textureId(viewer, deviceID))
        {
            ImGui::Image(id, surfaceSize);
        }
        else
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(surfaceMin, ImVec2(surfaceMin.x + surfaceSize.x, surfaceMin.y + surfaceSize.y), IM_COL32(23, 27, 29, 255));
            drawList->AddText(ImVec2(surfaceMin.x + 14.0f, surfaceMin.y + 14.0f), IM_COL32(230, 236, 238, 255), "Waiting for CEF paint");
        }

        forwardInput(cefUi, frame, surfaceMin, surfaceSize);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

uint32_t HtmlPanel::cefInputModifiers()
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

void HtmlPanel::updateTexture(const vsgcef::CefSurfaceFrame& frame)
{
    const auto& snapshot = frame.snapshot;
    if (!snapshot.available || snapshot.width <= 0 || snapshot.height <= 0 || frame.bgra.empty()) return;
    if (texture_.paintCount == snapshot.paintCount && texture_.texture) return;

    const auto width = static_cast<uint32_t>(snapshot.width);
    const auto height = static_cast<uint32_t>(snapshot.height);
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (frame.bgra.size() < expected) return;

    const bool needsNewTexture = !texture_.imageData ||
        texture_.imageData->width() != width ||
        texture_.imageData->height() != height;
    if (needsNewTexture)
    {
        texture_.imageData = vsg::ubvec4Array2D::create(width, height);
        texture_.imageData->properties.format = VK_FORMAT_B8G8R8A8_UNORM;
        texture_.imageData->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        texture_.texture = {};
        texture_.compiled = false;
    }

    std::memcpy(reinterpret_cast<uint8_t*>(texture_.imageData->dataPointer()), frame.bgra.data(), expected);
    texture_.imageData->dirty();
    if (!texture_.texture) texture_.texture = vsgImGui::Texture::create(texture_.imageData);
    texture_.paintCount = snapshot.paintCount;
}

ImTextureID HtmlPanel::textureId(vsg::observer_ptr<vsg::Viewer> viewer, uint32_t deviceID)
{
    if (!texture_.texture) return {};
    if (!texture_.compiled)
    {
        auto viewerRef = vsg::ref_ptr<vsg::Viewer>(viewer);
        if (!viewerRef || !viewerRef->compileManager) return {};

        auto result = viewerRef->compileManager->compile(texture_.texture);
        if (!result) return {};

        updateViewer(*viewerRef, result);
        texture_.compiled = true;
    }

    return texture_.texture->id(deviceID);
}

void HtmlPanel::forwardInput(const std::shared_ptr<vsgcef::CefUi>& cefUi,
                             const vsgcef::CefSurfaceFrame& frame,
                             const ImVec2& surfaceMin,
                             const ImVec2& surfaceSize)
{
    ImGui::SetCursorScreenPos(surfaceMin);
    ImGui::InvisibleButton(inputId_.c_str(), surfaceSize);

    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetMousePos();
    const int localX = std::clamp(static_cast<int>(mouse.x - surfaceMin.x), 0, static_cast<int>(surfaceSize.x) - 1);
    const int localY = std::clamp(static_cast<int>(mouse.y - surfaceMin.y), 0, static_cast<int>(surfaceSize.y) - 1);
    const int browserWidth = frame.snapshot.width > 0 ? frame.snapshot.width : static_cast<int>(surfaceSize.x);
    const int browserHeight = frame.snapshot.height > 0 ? frame.snapshot.height : static_cast<int>(surfaceSize.y);
    const int browserX = std::clamp(static_cast<int>((static_cast<float>(localX) / surfaceSize.x) * static_cast<float>(browserWidth)), 0, browserWidth - 1);
    const int browserY = std::clamp(static_cast<int>((static_cast<float>(localY) / surfaceSize.y) * static_cast<float>(browserHeight)), 0, browserHeight - 1);
    const uint32_t modifiers = cefInputModifiers();

    if (hovered)
    {
        cefUi->sendMouseMove(surfaceId_, browserX, browserY, modifiers);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            focused_ = true;
            cefUi->setFocus(surfaceId_, true);
            cefUi->sendMouseClick(surfaceId_, browserX, browserY, modifiers, vsgcef::CefMouseButton::Left, false, 1);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            cefUi->sendMouseClick(surfaceId_, browserX, browserY, modifiers, vsgcef::CefMouseButton::Left, true, 1);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            focused_ = true;
            cefUi->setFocus(surfaceId_, true);
            cefUi->sendMouseClick(surfaceId_, browserX, browserY, modifiers, vsgcef::CefMouseButton::Right, false, 1);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            cefUi->sendMouseClick(surfaceId_, browserX, browserY, modifiers, vsgcef::CefMouseButton::Right, true, 1);
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)
        {
            cefUi->sendMouseWheel(surfaceId_,
                                  browserX,
                                  browserY,
                                  modifiers,
                                  static_cast<int>(io.MouseWheelH * 120.0f),
                                  static_cast<int>(io.MouseWheel * 120.0f));
        }
    }

    if (!focused_) return;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    for (auto character : io.InputQueueCharacters)
    {
        if (character == 0) continue;
        const auto codepoint = static_cast<uint32_t>(character);
        cefUi->sendKey(surfaceId_, static_cast<int>(codepoint), modifiers, false);
        cefUi->sendKeyChar(surfaceId_, codepoint, modifiers);
        cefUi->sendKey(surfaceId_, static_cast<int>(codepoint), modifiers, true);
    }

    constexpr std::array<std::pair<ImGuiKey, int>, 12> keys{{
        {ImGuiKey_Backspace, 0x08},
        {ImGuiKey_Tab, 0x09},
        {ImGuiKey_Enter, 0x0D},
        {ImGuiKey_Escape, 0x1B},
        {ImGuiKey_Home, 0x24},
        {ImGuiKey_End, 0x23},
        {ImGuiKey_LeftArrow, 0x25},
        {ImGuiKey_UpArrow, 0x26},
        {ImGuiKey_RightArrow, 0x27},
        {ImGuiKey_DownArrow, 0x28},
        {ImGuiKey_Insert, 0x2D},
        {ImGuiKey_Delete, 0x2E}}};
    for (const auto& key : keys)
    {
        if (ImGui::IsKeyPressed(key.first, false)) cefUi->sendKey(surfaceId_, key.second, modifiers, false);
        if (ImGui::IsKeyReleased(key.first)) cefUi->sendKey(surfaceId_, key.second, modifiers, true);
    }
}

} // namespace htmlui
