#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vsgcef {

enum class CefSurfaceId
{
    Stats,
    Sorting
};

enum class CefMouseButton
{
    Left,
    Right,
    Middle
};

enum CefInputModifier : uint32_t
{
    CefInputModifierNone = 0,
    CefInputModifierShift = 1u << 0,
    CefInputModifierControl = 1u << 1,
    CefInputModifierAlt = 1u << 2,
    CefInputModifierLeftMouseButton = 1u << 3,
    CefInputModifierMiddleMouseButton = 1u << 4,
    CefInputModifierRightMouseButton = 1u << 5
};

struct CefSurfaceSnapshot
{
    bool available = false;
    bool browserCreated = false;
    bool dirty = false;
    int width = 0;
    int height = 0;
    uint64_t paintCount = 0;
    std::string url;
};

struct CefSurfaceFrame
{
    CefSurfaceSnapshot snapshot;
    std::vector<uint8_t> bgra;
};

class CefUi
{
public:
    static std::shared_ptr<CefUi> create(int argc, char** argv, const std::string& uiDirectory);
    ~CefUi();

    CefUi(const CefUi&) = delete;
    CefUi& operator=(const CefUi&) = delete;

    int exitCode() const { return exitCode_; }
    bool initialized() const { return initialized_; }
    void createBrowsers();
    void doMessageLoopWork();

    CefSurfaceSnapshot statsSnapshot() const;
    CefSurfaceSnapshot sortingSnapshot() const;
    CefSurfaceFrame statsFrame() const;
    CefSurfaceFrame sortingFrame() const;

    void executeJavaScript(CefSurfaceId surfaceId, const std::string& script);
    void sendMouseMove(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers);
    void sendMouseClick(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers, CefMouseButton button, bool mouseUp, int clickCount);
    void sendMouseWheel(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers, int deltaX, int deltaY);
    void sendKey(CefSurfaceId surfaceId, int windowsKeyCode, uint32_t modifiers, bool keyUp);
    void sendKeyChar(CefSurfaceId surfaceId, uint32_t character, uint32_t modifiers);
    void setFocus(CefSurfaceId surfaceId, bool focused);

private:
    CefUi() = default;

    bool initialize(int argc, char** argv, const std::string& uiDirectory);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    int exitCode_ = -1;
    bool initialized_ = false;
};

} // namespace vsgcef
