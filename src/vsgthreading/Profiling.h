#pragma once

#ifdef VSGCEF_ENABLE_TRACY
#include <tracy/Tracy.hpp>

#define VSGCEF_ZONE(name) ZoneScopedN(name)
#define VSGCEF_FRAME_MARK(name) FrameMarkNamed(name)
#define VSGCEF_THREAD_NAME(name) tracy::SetThreadName(name)
#define VSGCEF_PLOT(name, value) TracyPlot(name, value)

#else

#define VSGCEF_ZONE(name) (void)0
#define VSGCEF_FRAME_MARK(name) (void)0
#define VSGCEF_THREAD_NAME(name) (void)0
#define VSGCEF_PLOT(name, value) (void)0

#endif
