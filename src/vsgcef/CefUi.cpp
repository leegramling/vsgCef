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
#include "include/cef_task_manager.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vsgcef {
namespace {

using Clock = std::chrono::steady_clock;

class VsgCefApp : public CefApp
                , public CefRenderProcessHandler
{
public:
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

    void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override
    {
        commandLine->AppendSwitch("disable-gpu");
        commandLine->AppendSwitch("disable-gpu-compositing");
        commandLine->AppendSwitch("disable-gpu-sandbox");
#if defined(__linux__)
        if (processType.empty() && !commandLine->HasSwitch("no-zygote")) commandLine->AppendSwitch("no-zygote");
#endif
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
    if (dict && dict->HasKey("action") && dict->GetType("action") == VTYPE_STRING)
    {
        command.type = dict->GetString("action").ToString();
        if (dict->HasKey("args"))
        {
            auto args = dict->GetValue("args")->Copy();
            command.argsJson = CefWriteJSON(args, JSON_WRITER_DEFAULT).ToString();
        }
        else
        {
            command.argsJson = "{}";
        }
        return command;
    }

    if (!dict || !dict->HasKey("type") || dict->GetType("type") != VTYPE_STRING)
    {
        errorMessage = "Request missing string field 'action' or 'type'.";
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
    else if (command.type == "setRobotSpeed" ||
             command.type == "setSensorNoise" ||
             command.type == "setCommsDropout" ||
             command.type == "setJamRate")
    {
        if (!payload || !payload->HasKey("value"))
        {
            errorMessage = command.type + " requires numeric payload.value.";
            return {};
        }

        const auto valueType = payload->GetType("value");
        if (valueType == VTYPE_DOUBLE)
            command.value = payload->GetDouble("value");
        else if (valueType == VTYPE_INT)
            command.value = static_cast<double>(payload->GetInt("value"));
        else
        {
            errorMessage = "payload.value must be numeric.";
            return {};
        }
    }
    else if (command.type == "setRobotAuto")
    {
        if (!payload || !payload->HasKey("enabled") || payload->GetType("enabled") != VTYPE_BOOL)
        {
            errorMessage = "setRobotAuto requires boolean payload.enabled.";
            return {};
        }
        command.enabled = payload->GetBool("enabled");
    }
    else if (command.type == "sendRobotCharge" ||
             command.type == "resetRobotFault" ||
             command.type == "addRushOrder")
    {
    }
    else if (command.type == "renameObject")
    {
        if (!payload || !payload->HasKey("id") || !payload->HasKey("name"))
        {
            errorMessage = "renameObject requires payload.id and payload.name.";
            return {};
        }

        const auto idType = payload->GetType("id");
        if (idType == VTYPE_INT)
            command.objectId = static_cast<uint64_t>(payload->GetInt("id"));
        else if (idType == VTYPE_DOUBLE)
            command.objectId = static_cast<uint64_t>(payload->GetDouble("id"));
        else
        {
            errorMessage = "payload.id must be numeric.";
            return {};
        }

        if (payload->GetType("name") != VTYPE_STRING)
        {
            errorMessage = "payload.name must be a string.";
            return {};
        }
        command.name = payload->GetString("name").ToString();
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

    bool resize(int width, int height)
    {
        width = std::max(1, width);
        height = std::max(1, height);

        std::lock_guard<std::mutex> lock(mutex_);
        if (width_ == width && height_ == height) return false;

        width_ = width;
        height_ = height;
        buffer_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u, 0u);
        dirty_ = true;
        ++paintCount_;
        return true;
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
    CefPanelMetrics metrics;
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

std::string taskIdsString(const CefTaskManager::TaskIdList& taskIds)
{
    std::ostringstream result;
    result << "[";
    for (std::size_t i = 0; i < taskIds.size(); ++i)
    {
        if (i != 0) result << ",";
        result << taskIds[i];
    }
    result << "]";
    return result.str();
}

CefPanelMetrics queryMetricsForSurface(const BrowserSurface& surface, const char* label, std::ofstream* log)
{
    CefPanelMetrics result;
    const bool hasClient = static_cast<bool>(surface.client);
    const bool hasBrowser = hasClient && surface.client->browser();
    if (log)
    {
        *log << label
             << " hasClient=" << hasClient
             << " hasBrowser=" << hasBrowser;
    }

    if (!hasBrowser)
    {
        if (log) *log << " status=no-browser\n";
        return result;
    }

    auto browser = surface.client->browser();
    result.browserId = browser->GetIdentifier();
    if (log) *log << " browserId=" << result.browserId;

    auto taskManager = CefTaskManager::GetTaskManager();
    if (!taskManager)
    {
        if (log) *log << " status=no-task-manager\n";
        return result;
    }

    CefTaskManager::TaskIdList taskIds;
    const bool gotTaskIds = taskManager->GetTaskIdsList(taskIds);
    if (log)
    {
        *log << " taskManager=1"
             << " taskCount=" << taskManager->GetTasksCount()
             << " gotTaskIds=" << gotTaskIds
             << " taskIds=" << taskIdsString(taskIds);
    }

    result.taskId = taskManager->GetTaskIdForBrowserId(result.browserId);
    if (log) *log << " taskId=" << result.taskId;
    if (result.taskId < 0)
    {
        if (log) *log << " status=no-browser-task\n";
        return result;
    }

    CefTaskInfo taskInfo;
    const bool gotTaskInfo = taskManager->GetTaskInfo(result.taskId, taskInfo);
    if (log) *log << " gotTaskInfo=" << gotTaskInfo;
    if (!gotTaskInfo)
    {
        if (log) *log << " status=no-task-info\n";
        return result;
    }

    result.available = true;
    result.cpuUsage = taskInfo.cpu_usage;
    result.numberOfProcessors = taskInfo.number_of_processors;
    result.memoryBytes = taskInfo.memory;
    result.gpuMemoryBytes = taskInfo.gpu_memory;
    if (log)
    {
        *log << " status=ok"
             << " title=\"" << CefString(&taskInfo.title).ToString() << "\""
             << " type=" << static_cast<int>(taskInfo.type)
             << " cpu=" << result.cpuUsage
             << " processors=" << result.numberOfProcessors
             << " memoryBytes=" << result.memoryBytes
             << " gpuMemoryBytes=" << result.gpuMemoryBytes
             << " gpuInflated=" << taskInfo.is_gpu_memory_inflated
             << "\n";
    }
    return result;
}

void refreshMetrics(BrowserSurface& surface, const char* label, std::ofstream* log)
{
    surface.metrics = queryMetricsForSurface(surface, label, log);
}

} // namespace

struct CefUi::Impl
{
    CefRefPtr<VsgCefApp> app;
    CefRefPtr<UiCommandHandler> commandHandler;
    BrowserSurface primary;
    BrowserSurface secondary;
    Clock::time_point lastMetricsLogTime;
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
#ifdef VSGCEF_CEF_RESOURCES_DIR
    const std::string resourcesPath = std::filesystem::absolute(VSGCEF_CEF_RESOURCES_DIR).string();
    CefString(&settings.resources_dir_path).FromASCII(resourcesPath.c_str());
#endif
#ifdef VSGCEF_CEF_LOCALES_DIR
    const std::string localesPath = std::filesystem::absolute(VSGCEF_CEF_LOCALES_DIR).string();
    CefString(&settings.locales_dir_path).FromASCII(localesPath.c_str());
#endif
    {
        std::ofstream metricsLog(std::filesystem::absolute("cef_metrics.log"), std::ios::trunc);
        metricsLog << "CEF metrics log\n";
    }

    {
        VSGCEF_ZONE("CefInitialize");
        if (!CefInitialize(mainArgs, settings, impl_->app.get(), nullptr))
        {
            std::cerr << "[vsgCef] CefInitialize failed." << std::endl;
            return false;
        }
    }

    const std::filesystem::path uiPath(uiDirectory);
    impl_->primary.url = fileUrl(uiPath / "stats.html");
    impl_->primary.width = 300;
    impl_->primary.height = 800;
    impl_->primary.renderHandler = new SurfaceRenderHandler(impl_->primary.width, impl_->primary.height, impl_->primary.url, "stats");
    impl_->primary.commandHandler = impl_->commandHandler;
    impl_->primary.client = new SurfaceClient(impl_->primary.renderHandler, impl_->primary.commandHandler);

    impl_->secondary.url = fileUrl(uiPath / "sorting-form.html");
    impl_->secondary.width = 560;
    impl_->secondary.height = 360;
    impl_->secondary.renderHandler = new SurfaceRenderHandler(impl_->secondary.width, impl_->secondary.height, impl_->secondary.url, "sorting");
    impl_->secondary.commandHandler = impl_->commandHandler;
    impl_->secondary.client = new SurfaceClient(impl_->secondary.renderHandler, impl_->secondary.commandHandler);

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
    createBrowser(impl_->primary);
    createBrowser(impl_->secondary);
}

void CefUi::doMessageLoopWork()
{
    VSGCEF_ZONE("CefUi::doMessageLoopWork");
    VSGCEF_THREAD_NAME("main");

    if (!initialized_ || !impl_) return;

    CefDoMessageLoopWork();

    const auto now = Clock::now();
    const bool writeLog = impl_->lastMetricsLogTime == Clock::time_point{} ||
        now - impl_->lastMetricsLogTime >= std::chrono::seconds(1);

    std::ofstream metricsLog;
    std::ofstream* metricsLogPtr = nullptr;
    if (writeLog)
    {
        impl_->lastMetricsLogTime = now;
        metricsLog.open(std::filesystem::absolute("cef_metrics.log"), std::ios::app);
        if (metricsLog)
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            metricsLog << "sample_ms=" << ms << "\n";
            metricsLogPtr = &metricsLog;
        }
    }

    refreshMetrics(impl_->primary, "stats", metricsLogPtr);
    refreshMetrics(impl_->secondary, "sorting", metricsLogPtr);
}

CefSurfaceSnapshot CefUi::primarySnapshot() const
{
    if (!initialized_ || !impl_ || !impl_->primary.renderHandler) return {};
    return impl_->primary.renderHandler->snapshot();
}

CefSurfaceSnapshot CefUi::secondarySnapshot() const
{
    if (!initialized_ || !impl_ || !impl_->secondary.renderHandler) return {};
    return impl_->secondary.renderHandler->snapshot();
}

CefSurfaceFrame CefUi::primaryFrame() const
{
    VSGCEF_ZONE("CefUi::primaryFrame");

    if (!initialized_ || !impl_ || !impl_->primary.renderHandler) return {};
    return impl_->primary.renderHandler->frame();
}

CefSurfaceFrame CefUi::secondaryFrame() const
{
    VSGCEF_ZONE("CefUi::secondaryFrame");

    if (!initialized_ || !impl_ || !impl_->secondary.renderHandler) return {};
    return impl_->secondary.renderHandler->frame();
}

CefPanelMetrics CefUi::primaryMetrics() const
{
    if (!initialized_ || !impl_) return {};
    return impl_->primary.metrics;
}

CefPanelMetrics CefUi::secondaryMetrics() const
{
    if (!initialized_ || !impl_) return {};
    return impl_->secondary.metrics;
}

void CefUi::resizeSurface(CefSurfaceId surfaceId, int width, int height)
{
    if (!initialized_ || !impl_) return;

    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
    if (!surface->renderHandler || !surface->renderHandler->resize(width, height)) return;

    surface->width = std::max(1, width);
    surface->height = std::max(1, height);
    if (surface->client && surface->client->browser())
    {
        auto host = surface->client->browser()->GetHost();
        if (host) host->WasResized();
    }
}

void CefUi::executeJavaScript(CefSurfaceId surfaceId, const std::string& script)
{
    VSGCEF_ZONE("CefUi::executeJavaScript");
    VSGCEF_PLOT("CEF ExecuteJavaScript bytes", static_cast<int64_t>(script.size()));

    if (!impl_) return;
    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
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
    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
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
    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
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
    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
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
    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
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
    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
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
    auto* surface = surfaceId == CefSurfaceId::Primary ? &impl_->primary : &impl_->secondary;
    if (!surface->client || !surface->client->browser()) return;
    auto host = surface->client->browser()->GetHost();
    if (host) host->SetFocus(focused);
}

} // namespace vsgcef
