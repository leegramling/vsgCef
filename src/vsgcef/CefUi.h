#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vsgcef {

enum class CefSurfaceId
{
    Primary,
    Secondary
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

struct CefPanelMetrics
{
    bool available = false;
    int browserId = -1;
    int64_t taskId = -1;
    double cpuUsage = 0.0;
    int numberOfProcessors = 0;
    int64_t memoryBytes = -1;
    int64_t gpuMemoryBytes = -1;
};

struct CefUiCommand
{
    std::string type;
    std::string argsJson;
    std::string name;
    bool paused = false;
    bool enabled = false;
    uint64_t objectId = 0;
    double objectsPerSecond = 0.0;
    double value = 0.0;
    uint32_t count = 0;
};

class CefUi
{
public:
    using CommandHandler = std::function<bool(const CefUiCommand& command, std::string& errorMessage)>;

    static std::shared_ptr<CefUi> create(int argc, char** argv, const std::string& uiDirectory);
    static std::shared_ptr<CefUi> create(int argc, char** argv, const std::string& uiDirectory, CommandHandler commandHandler);
    ~CefUi();

    CefUi(const CefUi&) = delete;
    CefUi& operator=(const CefUi&) = delete;

    int exitCode() const { return exitCode_; }
    bool initialized() const { return initialized_; }
    void createBrowsers();
    void doMessageLoopWork();

    CefSurfaceSnapshot primarySnapshot() const;
    CefSurfaceSnapshot secondarySnapshot() const;
    CefSurfaceFrame primaryFrame() const;
    CefSurfaceFrame secondaryFrame() const;
    CefPanelMetrics primaryMetrics() const;
    CefPanelMetrics secondaryMetrics() const;

    CefSurfaceSnapshot statsSnapshot() const { return primarySnapshot(); }
    CefSurfaceSnapshot sortingSnapshot() const { return secondarySnapshot(); }
    CefSurfaceFrame statsFrame() const { return primaryFrame(); }
    CefSurfaceFrame sortingFrame() const { return secondaryFrame(); }
    CefPanelMetrics statsMetrics() const { return primaryMetrics(); }
    CefPanelMetrics sortingMetrics() const { return secondaryMetrics(); }

    void resizeSurface(CefSurfaceId surfaceId, int width, int height);
    void executeJavaScript(CefSurfaceId surfaceId, const std::string& script);
    void sendMouseMove(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers);
    void sendMouseClick(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers, CefMouseButton button, bool mouseUp, int clickCount);
    void sendMouseWheel(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers, int deltaX, int deltaY);
    void sendKey(CefSurfaceId surfaceId, int windowsKeyCode, uint32_t modifiers, bool keyUp);
    void sendKeyChar(CefSurfaceId surfaceId, uint32_t character, uint32_t modifiers);
    void setFocus(CefSurfaceId surfaceId, bool focused);

private:
    CefUi() = default;

    bool initialize(int argc, char** argv, const std::string& uiDirectory, CommandHandler commandHandler);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    int exitCode_ = -1;
    bool initialized_ = false;
};

} // namespace vsgcef
