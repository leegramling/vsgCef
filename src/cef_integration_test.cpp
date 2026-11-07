#include "vsgcef/CefUi.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

namespace {

int g_argc = 0;
char** g_argv = nullptr;

TEST(CefRuntimeLayoutTest, RequiredFilesAreBesideExecutable)
{
    ASSERT_NE(g_argc, 0);
    ASSERT_NE(g_argv, nullptr);
    ASSERT_NE(g_argv[0], nullptr);

    const std::filesystem::path executablePath =
        std::filesystem::absolute(std::filesystem::path(g_argv[0]));
    const std::filesystem::path executableDir = executablePath.parent_path();
    const char* ldLibraryPath = std::getenv("LD_LIBRARY_PATH");

    std::cout << "CEF integration executable: " << executablePath << std::endl;
    std::cout << "LD_LIBRARY_PATH: "
              << (ldLibraryPath ? ldLibraryPath : "<unset>") << std::endl;

#ifdef _WIN32
    const auto libcefName = "libcef.dll";
#else
    const auto libcefName = "libcef.so";
#endif

    for (const auto* fileName : {libcefName, "icudtl.dat"})
    {
        const auto filePath = executableDir / fileName;
        ASSERT_TRUE(std::filesystem::is_regular_file(filePath))
            << "Required CEF runtime file is missing beside the executable: "
            << filePath;
    }
}

class CefUiIntegrationTest : public testing::Test
{
protected:
    void SetUp() override
    {
        cefUi_ = vsgcef::CefUi::create(g_argc, g_argv, VSGCEF_CEF_UI_DIR);
        if (!cefUi_->initialized())
            GTEST_SKIP() << "CEF child process returned from CefExecuteProcess()";
    }

    void TearDown() override
    {
        // CefUi::~CefUi calls CefShutdown(). Keeping this in fixture teardown
        // makes clean CEF shutdown part of every test execution.
        cefUi_.reset();
    }

    std::shared_ptr<vsgcef::CefUi> cefUi_;
};

TEST_F(CefUiIntegrationTest, CreatesSurfacePaintsExpectedTextureAndShutsDown)
{
    constexpr int expectedWidth = 192;
    constexpr int expectedHeight = 128;
    const auto htmlFile = (std::filesystem::path(VSGCEF_CEF_UI_DIR) / "stats.html").string();
    const auto secondHtmlFile = (std::filesystem::path(VSGCEF_CEF_UI_DIR) / "sorting-form.html").string();
    const auto secondUrl = std::string("file://") + std::filesystem::absolute(secondHtmlFile).string();

    ASSERT_TRUE(cefUi_->addSurface("integration-test", htmlFile, expectedWidth, expectedHeight));

    cefUi_->createBrowsers();
    // Exercise the same resize notification used by HtmlPanel. This also
    // ensures CEF schedules a PET_VIEW paint for the off-screen surface.
    cefUi_->resizeSurface("integration-test", expectedWidth + 1, expectedHeight + 1);
    cefUi_->resizeSurface("integration-test", expectedWidth, expectedHeight);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    vsgcef::CefSurfaceFrame frame;
    while (std::chrono::steady_clock::now() < deadline)
    {
        cefUi_->doMessageLoopWork();
        frame = cefUi_->surfaceFrame("integration-test");
        if (frame.snapshot.browserCreated &&
            frame.snapshot.width == expectedWidth &&
            frame.snapshot.height == expectedHeight &&
            frame.snapshot.paintCount > 0 &&
            !frame.bgra.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const std::size_t expectedBytes = static_cast<std::size_t>(expectedWidth) *
        static_cast<std::size_t>(expectedHeight) * 4u;
    const auto initialUrl = frame.snapshot.url;
    ASSERT_TRUE(frame.snapshot.available);
    EXPECT_TRUE(frame.snapshot.browserCreated);
    EXPECT_EQ(frame.snapshot.width, expectedWidth);
    EXPECT_EQ(frame.snapshot.height, expectedHeight);
    EXPECT_GT(frame.snapshot.paintCount, 0u);
    EXPECT_EQ(frame.bgra.size(), expectedBytes);
    EXPECT_NE(static_cast<std::size_t>(std::count(frame.bgra.begin(), frame.bgra.end(), uint8_t{0})),
              frame.bgra.size());

    const auto navigationPaintCount = frame.snapshot.paintCount;
    cefUi_->loadUrl("integration-test", secondUrl);
    bool reachedSecondPage = false;
    const auto navigationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < navigationDeadline)
    {
        cefUi_->doMessageLoopWork();
        frame = cefUi_->surfaceFrame("integration-test");
        if (frame.snapshot.url == secondUrl &&
            frame.snapshot.paintCount > navigationPaintCount &&
            frame.snapshot.width == expectedWidth &&
            frame.snapshot.height == expectedHeight &&
            frame.bgra.size() == expectedBytes)
        {
            reachedSecondPage = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(reachedSecondPage);

    ASSERT_TRUE(cefUi_->goBack("integration-test"));
    bool returnedToFirstPage = false;
    const auto backDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < backDeadline)
    {
        cefUi_->doMessageLoopWork();
        frame = cefUi_->surfaceFrame("integration-test");
        if (frame.snapshot.url == initialUrl &&
            frame.snapshot.paintCount > navigationPaintCount &&
            frame.snapshot.width == expectedWidth &&
            frame.snapshot.height == expectedHeight &&
            frame.bgra.size() == expectedBytes)
        {
            returnedToFirstPage = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(returnedToFirstPage);
}

} // namespace

int main(int argc, char** argv)
{
#if defined(__linux__)
    // Match a minimal CI container without DBus session/system-bus addresses.
    unsetenv("DBUS_SESSION_BUS_ADDRESS");
    unsetenv("DBUS_SYSTEM_BUS_ADDRESS");
#endif
    ::testing::InitGoogleTest(&argc, argv);
    g_argc = argc;
    g_argv = argv;
    return RUN_ALL_TESTS();
}
