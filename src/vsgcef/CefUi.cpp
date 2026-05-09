#include "vsgcef/CefUi.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

namespace vsgcef {
namespace {

class VsgCefApp : public CefApp
{
public:
    void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override
    {
        (void)processType;
        commandLine->AppendSwitch("--disable-gpu");
        commandLine->AppendSwitch("--disable-gpu-compositing");
        commandLine->AppendSwitch("--disable-gpu-sandbox");
        commandLine->AppendSwitch("--disable-software-rasterizer");
        commandLine->AppendSwitch("--no-sandbox");
        commandLine->AppendSwitch("--disable-setuid-sandbox");
        commandLine->AppendSwitch("--disable-dev-shm-usage");
        commandLine->AppendSwitch("--disable-extensions");
        commandLine->AppendSwitch("--disable-plugins");
        commandLine->AppendSwitch("--disable-background-networking");
        commandLine->AppendSwitch("--disable-component-update");
        commandLine->AppendSwitch("--disable-default-apps");
        commandLine->AppendSwitch("--disable-sync");
        commandLine->AppendSwitch("--metrics-recording-only");
        commandLine->AppendSwitchWithValue("--disable-features", "PushMessaging,MediaRouter,OptimizationHints");
    }

private:
    IMPLEMENT_REFCOUNTING(VsgCefApp);
};

class SurfaceRenderHandler : public CefRenderHandler
{
public:
    SurfaceRenderHandler(int width, int height, std::string url) :
        width_(width),
        height_(height),
        url_(std::move(url))
    {
        buffer_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u);
    }

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override
    {
        (void)browser;
        std::lock_guard<std::mutex> lock(mutex_);
        rect = CefRect(0, 0, width_, height_);
    }

    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width,
                 int height) override
    {
        (void)browser;
        (void)type;
        (void)dirtyRects;

        std::lock_guard<std::mutex> lock(mutex_);
        width_ = width;
        height_ = height;
        const auto byteCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
        buffer_.resize(byteCount);
        std::memcpy(buffer_.data(), buffer, byteCount);
        dirty_ = true;
        ++paintCount_;
    }

    CefSurfaceSnapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        CefSurfaceSnapshot result;
        result.available = true;
        result.browserCreated = browserCreated_;
        result.dirty = dirty_;
        result.width = width_;
        result.height = height_;
        result.paintCount = paintCount_;
        result.url = url_;
        return result;
    }

    CefSurfaceFrame frame() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        CefSurfaceFrame result;
        result.snapshot.available = true;
        result.snapshot.browserCreated = browserCreated_;
        result.snapshot.dirty = dirty_;
        result.snapshot.width = width_;
        result.snapshot.height = height_;
        result.snapshot.paintCount = paintCount_;
        result.snapshot.url = url_;
        result.bgra = buffer_;
        return result;
    }

    void markBrowserCreated()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        browserCreated_ = true;
    }

    void markBrowserClosed()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        browserCreated_ = false;
    }

private:
    mutable std::mutex mutex_;
    std::vector<uint8_t> buffer_;
    int width_ = 0;
    int height_ = 0;
    bool dirty_ = false;
    bool browserCreated_ = false;
    uint64_t paintCount_ = 0;
    std::string url_;

    IMPLEMENT_REFCOUNTING(SurfaceRenderHandler);
};

class SurfaceClient : public CefClient, public CefLifeSpanHandler
{
public:
    explicit SurfaceClient(CefRefPtr<SurfaceRenderHandler> renderHandler) :
        renderHandler_(std::move(renderHandler))
    {
    }

    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return renderHandler_; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        browser_ = std::move(browser);
        renderHandler_->markBrowserCreated();
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
        (void)browser;
        browser_ = nullptr;
        renderHandler_->markBrowserClosed();
    }

    CefRefPtr<CefBrowser> browser() const { return browser_; }

private:
    CefRefPtr<SurfaceRenderHandler> renderHandler_;
    CefRefPtr<CefBrowser> browser_;

    IMPLEMENT_REFCOUNTING(SurfaceClient);
};

struct BrowserSurface
{
    CefRefPtr<SurfaceRenderHandler> renderHandler;
    CefRefPtr<SurfaceClient> client;
    std::string url;
    int width = 0;
    int height = 0;
};

std::string fileUrl(const std::filesystem::path& path)
{
    return std::string("file://") + std::filesystem::absolute(path).lexically_normal().string();
}

void createBrowser(BrowserSurface& surface)
{
    if (surface.client && surface.client->browser()) return;

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(0);

    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = 30;

    CefBrowserHost::CreateBrowser(windowInfo, surface.client, surface.url, browserSettings, nullptr, nullptr);
}

} // namespace

struct CefUi::Impl
{
    CefRefPtr<VsgCefApp> app;
    BrowserSurface stats;
    BrowserSurface sorting;
};

namespace {

int cefModifiers(uint32_t modifiers)
{
    int result = 0;
    if ((modifiers & CefInputModifierShift) != 0) result |= EVENTFLAG_SHIFT_DOWN;
    if ((modifiers & CefInputModifierControl) != 0) result |= EVENTFLAG_CONTROL_DOWN;
    if ((modifiers & CefInputModifierAlt) != 0) result |= EVENTFLAG_ALT_DOWN;
    if ((modifiers & CefInputModifierLeftMouseButton) != 0) result |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    if ((modifiers & CefInputModifierMiddleMouseButton) != 0) result |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    if ((modifiers & CefInputModifierRightMouseButton) != 0) result |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    return result;
}

cef_mouse_button_type_t cefMouseButton(CefMouseButton button)
{
    switch (button)
    {
    case CefMouseButton::Right: return MBT_RIGHT;
    case CefMouseButton::Middle: return MBT_MIDDLE;
    case CefMouseButton::Left:
    default: return MBT_LEFT;
    }
}

} // namespace

std::shared_ptr<CefUi> CefUi::create(int argc, char** argv, const std::string& uiDirectory)
{
    auto cefUi = std::shared_ptr<CefUi>(new CefUi());
    cefUi->initialize(argc, argv, uiDirectory);
    return cefUi;
}

bool CefUi::initialize(int argc, char** argv, const std::string& uiDirectory)
{
    impl_ = std::make_unique<Impl>();
    impl_->app = new VsgCefApp();

    CefMainArgs mainArgs(argc, argv);
    exitCode_ = CefExecuteProcess(mainArgs, nullptr, nullptr);
    if (exitCode_ >= 0) return false;

    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = true;
    settings.external_message_pump = false;
    settings.multi_threaded_message_loop = false;
    settings.log_severity = LOGSEVERITY_INFO;
    const std::string cachePath = std::filesystem::absolute("cef_cache").string();
    CefString(&settings.root_cache_path).FromASCII(cachePath.c_str());
    CefString(&settings.log_file).FromASCII("cef_ui.log");

    if (!CefInitialize(mainArgs, settings, impl_->app.get(), nullptr))
    {
        std::cerr << "[vsgCef] CefInitialize failed." << std::endl;
        return false;
    }

    const std::filesystem::path uiPath(uiDirectory);
    impl_->stats.url = fileUrl(uiPath / "stats.html");
    impl_->stats.width = 340;
    impl_->stats.height = 420;
    impl_->stats.renderHandler = new SurfaceRenderHandler(impl_->stats.width, impl_->stats.height, impl_->stats.url);
    impl_->stats.client = new SurfaceClient(impl_->stats.renderHandler);

    impl_->sorting.url = fileUrl(uiPath / "sorting-form.html");
    impl_->sorting.width = 560;
    impl_->sorting.height = 360;
    impl_->sorting.renderHandler = new SurfaceRenderHandler(impl_->sorting.width, impl_->sorting.height, impl_->sorting.url);
    impl_->sorting.client = new SurfaceClient(impl_->sorting.renderHandler);

    initialized_ = true;
    return true;
}

CefUi::~CefUi()
{
    if (initialized_) CefShutdown();
}

void CefUi::createBrowsers()
{
    if (!initialized_ || !impl_) return;
    createBrowser(impl_->stats);
    createBrowser(impl_->sorting);
}

void CefUi::doMessageLoopWork()
{
    if (initialized_) CefDoMessageLoopWork();
}

CefSurfaceSnapshot CefUi::statsSnapshot() const
{
    if (!initialized_ || !impl_ || !impl_->stats.renderHandler) return {};
    return impl_->stats.renderHandler->snapshot();
}

CefSurfaceSnapshot CefUi::sortingSnapshot() const
{
    if (!initialized_ || !impl_ || !impl_->sorting.renderHandler) return {};
    return impl_->sorting.renderHandler->snapshot();
}

CefSurfaceFrame CefUi::statsFrame() const
{
    if (!initialized_ || !impl_ || !impl_->stats.renderHandler) return {};
    return impl_->stats.renderHandler->frame();
}

CefSurfaceFrame CefUi::sortingFrame() const
{
    if (!initialized_ || !impl_ || !impl_->sorting.renderHandler) return {};
    return impl_->sorting.renderHandler->frame();
}

void CefUi::executeJavaScript(CefSurfaceId surfaceId, const std::string& script)
{
    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Stats ? &impl_->stats : &impl_->sorting;
    if (!surface->client || !surface->client->browser()) return;

    auto browser = surface->client->browser();
    auto frame = browser->GetMainFrame();
    if (!frame) return;

    frame->ExecuteJavaScript(script, surface->url, 0);
}

void CefUi::sendMouseMove(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers)
{
    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Stats ? &impl_->stats : &impl_->sorting;
    if (!surface->client || !surface->client->browser()) return;
    auto host = surface->client->browser()->GetHost();
    if (!host) return;

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = cefModifiers(modifiers);
    host->SendMouseMoveEvent(event, false);
}

void CefUi::sendMouseClick(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers, CefMouseButton button, bool mouseUp, int clickCount)
{
    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Stats ? &impl_->stats : &impl_->sorting;
    if (!surface->client || !surface->client->browser()) return;
    auto host = surface->client->browser()->GetHost();
    if (!host) return;

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = cefModifiers(modifiers);
    host->SendMouseClickEvent(event, cefMouseButton(button), mouseUp, clickCount);
    if (!mouseUp) host->SetFocus(true);
}

void CefUi::sendMouseWheel(CefSurfaceId surfaceId, int x, int y, uint32_t modifiers, int deltaX, int deltaY)
{
    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Stats ? &impl_->stats : &impl_->sorting;
    if (!surface->client || !surface->client->browser()) return;
    auto host = surface->client->browser()->GetHost();
    if (!host) return;

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = cefModifiers(modifiers);
    host->SendMouseWheelEvent(event, deltaX, deltaY);
}

void CefUi::sendKeyChar(CefSurfaceId surfaceId, uint32_t character, uint32_t modifiers)
{
    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Stats ? &impl_->stats : &impl_->sorting;
    if (!surface->client || !surface->client->browser()) return;
    auto host = surface->client->browser()->GetHost();
    if (!host) return;

    CefKeyEvent event;
    event.type = KEYEVENT_CHAR;
    event.windows_key_code = static_cast<int>(character);
    event.native_key_code = static_cast<int>(character);
    event.character = static_cast<char16_t>(character);
    event.unmodified_character = static_cast<char16_t>(character);
    event.modifiers = cefModifiers(modifiers);
    host->SendKeyEvent(event);
}

void CefUi::sendKey(CefSurfaceId surfaceId, int windowsKeyCode, uint32_t modifiers, bool keyUp)
{
    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Stats ? &impl_->stats : &impl_->sorting;
    if (!surface->client || !surface->client->browser()) return;
    auto host = surface->client->browser()->GetHost();
    if (!host) return;

    CefKeyEvent event;
    event.type = keyUp ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
    event.windows_key_code = windowsKeyCode;
    event.native_key_code = windowsKeyCode;
    event.modifiers = cefModifiers(modifiers);
    host->SendKeyEvent(event);
}

void CefUi::setFocus(CefSurfaceId surfaceId, bool focused)
{
    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Stats ? &impl_->stats : &impl_->sorting;
    if (!surface->client || !surface->client->browser()) return;
    auto host = surface->client->browser()->GetHost();
    if (host) host->SetFocus(focused);
}

} // namespace vsgcef
