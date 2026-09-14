#include "vsgcef/CefUi.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <thread>

namespace {

int g_argc = 0;
char** g_argv = nullptr;

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
    ASSERT_TRUE(frame.snapshot.available);
    EXPECT_TRUE(frame.snapshot.browserCreated);
    EXPECT_EQ(frame.snapshot.width, expectedWidth);
    EXPECT_EQ(frame.snapshot.height, expectedHeight);
    EXPECT_GT(frame.snapshot.paintCount, 0u);
    EXPECT_EQ(frame.bgra.size(), expectedBytes);
}

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    g_argc = argc;
    g_argv = argv;
    return RUN_ALL_TESTS();
}
