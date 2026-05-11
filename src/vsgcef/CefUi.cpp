#include "vsgcef/CefUi.h"

#include "vsgthreading/Profiling.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_parser.h"
#include "include/cef_render_handler.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vsgcef {
namespace {

class VsgCefApp : public CefApp
                , public CefRenderProcessHandler
{
public:
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

    void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override
    {
        (void)processType;
        commandLine->AppendSwitch("disable-gpu");
        commandLine->AppendSwitch("disable-gpu-compositing");
        commandLine->AppendSwitch("disable-gpu-sandbox");
        commandLine->AppendSwitch("no-sandbox");
        commandLine->AppendSwitch("disable-setuid-sandbox");
        commandLine->AppendSwitch("disable-dev-shm-usage");
        commandLine->AppendSwitch("disable-extensions");
        commandLine->AppendSwitch("disable-plugins");
        commandLine->AppendSwitch("disable-background-networking");
        commandLine->AppendSwitch("disable-component-update");
        commandLine->AppendSwitch("disable-default-apps");
        commandLine->AppendSwitch("disable-sync");
        commandLine->AppendSwitch("metrics-recording-only");
        commandLine->AppendSwitchWithValue("disable-features", "PushMessaging,MediaRouter,OptimizationHints");
    }

    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override
    {
        if (!messageRouter_)
        {
            CefMessageRouterConfig config;
            messageRouter_ = CefMessageRouterRendererSide::Create(config);
        }
        messageRouter_->OnContextCreated(browser, frame, context);
    }

    void OnContextReleased(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override
    {
        if (messageRouter_) messageRouter_->OnContextReleased(browser, frame, context);
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId sourceProcess,
                                  CefRefPtr<CefProcessMessage> message) override
    {
        if (messageRouter_) return messageRouter_->OnProcessMessageReceived(browser, frame, sourceProcess, message);
        return false;
    }

private:
    CefRefPtr<CefMessageRouterRendererSide> messageRouter_;

    IMPLEMENT_REFCOUNTING(VsgCefApp);
};

CefUiCommand commandFromRequest(const CefString& request, std::string& errorMessage)
{
    CefUiCommand command;
    CefRefPtr<CefValue> root = CefParseJSON(request, JSON_PARSER_RFC);
    if (!root || root->GetType() != VTYPE_DICTIONARY)
    {
        errorMessage = "Expected JSON object request.";
        return command;
    }

    auto dict = root->GetDictionary();
    if (!dict || !dict->HasKey("type") || dict->GetType("type") != VTYPE_STRING)
    {
        errorMessage = "Request missing string field 'type'.";
        return command;
    }

    command.type = dict->GetString("type").ToString();
    auto payload = dict->GetDictionary("payload");

    if (command.type == "setPaused")
    {
        if (!payload || !payload->HasKey("paused") || payload->GetType("paused") != VTYPE_BOOL)
        {
            errorMessage = "setPaused requires payload.paused.";
            return {};
        }
        command.paused = payload->GetBool("paused");
    }
    else if (command.type == "setSpawnRate")
    {
        if (!payload || !payload->HasKey("objectsPerSecond"))
        {
            errorMessage = "setSpawnRate requires payload.objectsPerSecond.";
            return {};
        }

        const auto valueType = payload->GetType("objectsPerSecond");
        if (valueType == VTYPE_DOUBLE)
            command.objectsPerSecond = payload->GetDouble("objectsPerSecond");
        else if (valueType == VTYPE_INT)
            command.objectsPerSecond = static_cast<double>(payload->GetInt("objectsPerSecond"));
        else
        {
            errorMessage = "payload.objectsPerSecond must be numeric.";
            return {};
        }
    }
    else if (command.type == "spawnBurst")
    {
        if (!payload || !payload->HasKey("count") || payload->GetType("count") != VTYPE_INT)
        {
            errorMessage = "spawnBurst requires integer payload.count.";
            return {};
        }

        const int count = payload->GetInt("count");
        if (count < 0)
        {
            errorMessage = "payload.count must be non-negative.";
            return {};
        }
        command.count = static_cast<uint32_t>(count);
    }
    else if (command.type == "clearObjects" || command.type == "mockSettingChanged" ||
             command.type == "mockTypeEnabledChanged" || command.type == "mockTypeSpawnChanged" ||
             command.type == "mockTypeSpeedChanged")
    {
        // These commands are valid bridge messages. The app decides which ones
        // affect simulation state and which ones are form-only notifications.
    }
    else
    {
        errorMessage = "Unknown command type: " + command.type;
        return {};
    }

    return command;
}

class UiCommandHandler : public CefMessageRouterBrowserSide::Handler
                       , public CefBaseRefCounted
{
public:
    explicit UiCommandHandler(CefUi::CommandHandler commandHandler) :
        commandHandler_(std::move(commandHandler))
    {
    }

    bool OnQuery(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int64_t queryId,
                 const CefString& request,
                 bool persistent,
                 CefRefPtr<Callback> callback) override
    {
        (void)browser;
        (void)frame;
        (void)queryId;
        (void)persistent;

        if (!commandHandler_)
        {
            callback->Failure(500, "CEF command handler is unavailable.");
            return true;
        }

        std::string errorMessage;
        CefUiCommand command = commandFromRequest(request, errorMessage);
        if (!errorMessage.empty())
        {
            callback->Failure(400, errorMessage);
            return true;
        }

        if (!commandHandler_(command, errorMessage))
        {
            callback->Failure(400, errorMessage.empty() ? "Command rejected." : errorMessage);
            return true;
        }

        callback->Success("ok");
        return true;
    }

private:
    CefUi::CommandHandler commandHandler_;

    IMPLEMENT_REFCOUNTING(UiCommandHandler);
};

class SurfaceRenderHandler : public CefRenderHandler
{
public:
    SurfaceRenderHandler(int width, int height, std::string url, std::string label) :
        width_(width),
        height_(height),
        url_(std::move(url)),
        label_(std::move(label))
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
        VSGCEF_ZONE("CEF SurfaceRenderHandler::OnPaint");

        (void)browser;
        (void)type;
        (void)dirtyRects;

        std::lock_guard<std::mutex> lock(mutex_);
        width_ = width;
        height_ = height;
        const auto byteCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
        if (label_ == "stats")
            VSGCEF_PLOT("CEF stats paint bytes", static_cast<int64_t>(byteCount));
        else
            VSGCEF_PLOT("CEF sorting paint bytes", static_cast<int64_t>(byteCount));
        buffer_.resize(byteCount);
        {
            VSGCEF_ZONE("Copy CEF paint buffer");
            std::memcpy(buffer_.data(), buffer, byteCount);
        }
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
    std::string label_;

    IMPLEMENT_REFCOUNTING(SurfaceRenderHandler);
};

class SurfaceClient : public CefClient, public CefLifeSpanHandler
{
public:
    SurfaceClient(CefRefPtr<SurfaceRenderHandler> renderHandler, CefRefPtr<UiCommandHandler> commandHandler) :
        renderHandler_(std::move(renderHandler)),
        commandHandler_(std::move(commandHandler))
    {
        CefMessageRouterConfig config;
        messageRouter_ = CefMessageRouterBrowserSide::Create(config);
        if (commandHandler_) messageRouter_->AddHandler(commandHandler_.get(), false);
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
        if (messageRouter_) messageRouter_->OnBeforeClose(browser);
        browser_ = nullptr;
        renderHandler_->markBrowserClosed();
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId sourceProcess,
                                  CefRefPtr<CefProcessMessage> message) override
    {
        if (messageRouter_ && messageRouter_->OnProcessMessageReceived(browser, frame, sourceProcess, message)) return true;
        return false;
    }

    CefRefPtr<CefBrowser> browser() const { return browser_; }

private:
    CefRefPtr<SurfaceRenderHandler> renderHandler_;
    CefRefPtr<CefMessageRouterBrowserSide> messageRouter_;
    CefRefPtr<UiCommandHandler> commandHandler_;
    CefRefPtr<CefBrowser> browser_;

    IMPLEMENT_REFCOUNTING(SurfaceClient);
};

struct BrowserSurface
{
    CefRefPtr<SurfaceRenderHandler> renderHandler;
    CefRefPtr<SurfaceClient> client;
    CefRefPtr<UiCommandHandler> commandHandler;
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
    VSGCEF_ZONE("CEF createBrowser");

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
    CefRefPtr<UiCommandHandler> commandHandler;
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
    return create(argc, argv, uiDirectory, {});
}

std::shared_ptr<CefUi> CefUi::create(int argc, char** argv, const std::string& uiDirectory, CommandHandler commandHandler)
{
    auto cefUi = std::shared_ptr<CefUi>(new CefUi());
    cefUi->initialize(argc, argv, uiDirectory, std::move(commandHandler));
    return cefUi;
}

bool CefUi::initialize(int argc, char** argv, const std::string& uiDirectory, CommandHandler commandHandler)
{
    VSGCEF_ZONE("CefUi::initialize");

    impl_ = std::make_unique<Impl>();
    impl_->app = new VsgCefApp();
    if (commandHandler) impl_->commandHandler = new UiCommandHandler(std::move(commandHandler));

#ifdef _WIN32
    (void)argc;
    (void)argv;
    CefMainArgs mainArgs(GetModuleHandle(nullptr));
#else
    CefMainArgs mainArgs(argc, argv);
#endif
    exitCode_ = CefExecuteProcess(mainArgs, impl_->app.get(), nullptr);
    if (exitCode_ >= 0) return false;

    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = true;
    settings.external_message_pump = false;
    settings.multi_threaded_message_loop = false;
    settings.log_severity = LOGSEVERITY_INFO;
    const std::string cachePath = std::filesystem::absolute("cef_cache").string();
    CefString(&settings.root_cache_path).FromASCII(cachePath.c_str());
    const std::string logPath = std::filesystem::absolute("cef_ui.log").string();
    CefString(&settings.log_file).FromASCII(logPath.c_str());

    {
        VSGCEF_ZONE("CefInitialize");
        if (!CefInitialize(mainArgs, settings, impl_->app.get(), nullptr))
        {
            std::cerr << "[vsgCef] CefInitialize failed." << std::endl;
            return false;
        }
    }

    const std::filesystem::path uiPath(uiDirectory);
    impl_->stats.url = fileUrl(uiPath / "stats.html");
    impl_->stats.width = 300;
    impl_->stats.height = 800;
    impl_->stats.renderHandler = new SurfaceRenderHandler(impl_->stats.width, impl_->stats.height, impl_->stats.url, "stats");
    impl_->stats.commandHandler = impl_->commandHandler;
    impl_->stats.client = new SurfaceClient(impl_->stats.renderHandler, impl_->stats.commandHandler);

    impl_->sorting.url = fileUrl(uiPath / "sorting-form.html");
    impl_->sorting.width = 560;
    impl_->sorting.height = 360;
    impl_->sorting.renderHandler = new SurfaceRenderHandler(impl_->sorting.width, impl_->sorting.height, impl_->sorting.url, "sorting");
    impl_->sorting.commandHandler = impl_->commandHandler;
    impl_->sorting.client = new SurfaceClient(impl_->sorting.renderHandler, impl_->sorting.commandHandler);

    initialized_ = true;
    return true;
}

CefUi::~CefUi()
{
    if (initialized_) CefShutdown();
}

void CefUi::createBrowsers()
{
    VSGCEF_ZONE("CefUi::createBrowsers");

    if (!initialized_ || !impl_) return;
    createBrowser(impl_->stats);
    createBrowser(impl_->sorting);
}

void CefUi::doMessageLoopWork()
{
    VSGCEF_ZONE("CefUi::doMessageLoopWork");
    VSGCEF_THREAD_NAME("main");

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
    VSGCEF_ZONE("CefUi::statsFrame");

    if (!initialized_ || !impl_ || !impl_->stats.renderHandler) return {};
    return impl_->stats.renderHandler->frame();
}

CefSurfaceFrame CefUi::sortingFrame() const
{
    VSGCEF_ZONE("CefUi::sortingFrame");

    if (!initialized_ || !impl_ || !impl_->sorting.renderHandler) return {};
    return impl_->sorting.renderHandler->frame();
}

void CefUi::executeJavaScript(CefSurfaceId surfaceId, const std::string& script)
{
    VSGCEF_ZONE("CefUi::executeJavaScript");
    VSGCEF_PLOT("CEF ExecuteJavaScript bytes", static_cast<int64_t>(script.size()));

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
    VSGCEF_ZONE("CefUi::sendMouseMove");

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
    VSGCEF_ZONE("CefUi::sendMouseClick");

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
    VSGCEF_ZONE("CefUi::sendMouseWheel");

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
    VSGCEF_ZONE("CefUi::sendKeyChar");

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
    VSGCEF_ZONE("CefUi::sendKey");

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
