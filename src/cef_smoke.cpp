#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <iostream>
#include <thread>

class SmokeCefApp : public CefApp
{
public:
    SmokeCefApp() = default;

    void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) override
    {
        (void)process_type;
        command_line->AppendSwitch("--disable-gpu");
        command_line->AppendSwitch("--disable-gpu-compositing");
        command_line->AppendSwitch("--disable-gpu-sandbox");
        command_line->AppendSwitch("--disable-software-rasterizer");
        command_line->AppendSwitch("--headless");
        command_line->AppendSwitch("--no-sandbox");
        command_line->AppendSwitch("--disable-setuid-sandbox");
        command_line->AppendSwitch("--no-zygote");
        command_line->AppendSwitch("--disable-dev-shm-usage");
        command_line->AppendSwitch("--disable-extensions");
        command_line->AppendSwitch("--disable-plugins");
        command_line->AppendSwitch("--disable-web-security");
        command_line->AppendSwitch("--disable-features=VizDisplayCompositor");
    }

private:
    IMPLEMENT_REFCOUNTING(SmokeCefApp);
    DISALLOW_COPY_AND_ASSIGN(SmokeCefApp);
};

int main(int argc, char* argv[])
{
    std::cout << "Starting vsgCef CEF initialization test..." << std::endl;

    CefMainArgs main_args(argc, argv);

    int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
    if (exit_code >= 0) return exit_code;

    CefSettings settings;
    settings.windowless_rendering_enabled = false;
    settings.no_sandbox = true;
    settings.log_severity = LOGSEVERITY_INFO;
    CefString(&settings.log_file).FromASCII("cef_smoke.log");

    CefRefPtr<SmokeCefApp> app = new SmokeCefApp();

    std::cout << "Calling CefInitialize..." << std::endl;
    const bool result = CefInitialize(main_args, settings, app.get(), nullptr);
    if (!result)
    {
        std::cerr << "ERROR: CefInitialize failed." << std::endl;
        return 1;
    }

    std::cout << "CEF initialized successfully." << std::endl;
    std::cout << "Running minimal message loop..." << std::endl;

    const auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() - start_time < timeout)
    {
        CefDoMessageLoopWork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "Message loop completed." << std::endl;
    std::cout << "Shutting down CEF..." << std::endl;
    CefShutdown();

    std::cout << "vsgCef CEF initialization test completed successfully." << std::endl;
    return 0;
}
