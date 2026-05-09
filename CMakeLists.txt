cmake_minimum_required(VERSION 3.22)

project(vsgCef LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(VSG_DEPS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vsg_deps/install" CACHE PATH "Path to VSG/vsgImGui install prefix")
if(EXISTS "${VSG_DEPS_INSTALL_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${VSG_DEPS_INSTALL_DIR}")
endif()

find_package(vsg CONFIG REQUIRED)
find_package(vsgImGui CONFIG REQUIRED)
find_package(Threads REQUIRED)
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
endif()

set(VSGCEF_DEFAULT_CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../imguiCef/cef_binary_143.0.14+gdd46a37+chromium-143.0.7499.193_linux64")
set(VSGCEF_DEFAULT_CEF_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../imguiCef/build")
set(VSGCEF_DEFAULT_CEF_WRAPPER_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../imguiCef/build/libcef_dll_wrapper.a")
if(WIN32)
    set(VSGCEF_DEFAULT_CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../imguiCef/cef_binary_windows64")
    set(VSGCEF_DEFAULT_CEF_RUNTIME_DIR "${VSGCEF_DEFAULT_CEF_ROOT}/Release")
    set(VSGCEF_DEFAULT_CEF_WRAPPER_LIBRARY "${VSGCEF_DEFAULT_CEF_ROOT}/build/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib")
endif()
set(VSGCEF_CEF_ROOT "${VSGCEF_DEFAULT_CEF_ROOT}" CACHE PATH "Path to a CEF binary distribution root")
set(VSGCEF_CEF_RUNTIME_DIR "${VSGCEF_DEFAULT_CEF_RUNTIME_DIR}" CACHE PATH "Path to a directory containing libcef.so and CEF runtime files")
set(VSGCEF_CEF_WRAPPER_LIBRARY "${VSGCEF_DEFAULT_CEF_WRAPPER_LIBRARY}" CACHE FILEPATH "Path to libcef_dll_wrapper.a")
option(VSGCEF_ENABLE_CEF "Enable CEF-dependent targets" ON)
option(VSGCEF_ENABLE_TRACY "Enable Tracy profiling instrumentation" OFF)
set(VSGCEF_TRACY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../vkRaw/lightweightvk/third-party/deps/src/tracy" CACHE PATH "Path to a Tracy source checkout")

if(VSGCEF_ENABLE_TRACY)
    set(VSGCEF_TRACY_CLIENT_CPP "${VSGCEF_TRACY_ROOT}/public/TracyClient.cpp")
    set(VSGCEF_TRACY_INCLUDE_DIR "${VSGCEF_TRACY_ROOT}/public")
    if(EXISTS "${VSGCEF_TRACY_CLIENT_CPP}" AND EXISTS "${VSGCEF_TRACY_INCLUDE_DIR}/tracy/Tracy.hpp")
        add_library(vsgCef_tracy STATIC "${VSGCEF_TRACY_CLIENT_CPP}")
        target_include_directories(vsgCef_tracy SYSTEM PUBLIC "${VSGCEF_TRACY_INCLUDE_DIR}")
        target_compile_definitions(vsgCef_tracy PUBLIC TRACY_ENABLE TRACY_ON_DEMAND)
        target_link_libraries(vsgCef_tracy PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
        if(MSVC)
            target_compile_options(vsgCef_tracy PRIVATE /w)
        else()
            target_compile_options(vsgCef_tracy PRIVATE -w)
        endif()
        set(VSGCEF_TRACY_AVAILABLE ON)
    else()
        message(WARNING "Tracy profiling disabled. Set VSGCEF_TRACY_ROOT to a Tracy source checkout.")
    endif()
endif()

if(VSGCEF_ENABLE_CEF)
    set(VSGCEF_CEF_INCLUDE_DIR "${VSGCEF_CEF_ROOT}/include")
    if(WIN32)
        set(VSGCEF_CEF_LIBCEF_LIBRARY "${VSGCEF_CEF_RUNTIME_DIR}/libcef.lib")
        set(VSGCEF_CEF_LIBCEF_RUNTIME "${VSGCEF_CEF_RUNTIME_DIR}/libcef.dll")
    else()
        set(VSGCEF_CEF_LIBCEF_LIBRARY "${VSGCEF_CEF_RUNTIME_DIR}/libcef.so")
        set(VSGCEF_CEF_LIBCEF_RUNTIME "${VSGCEF_CEF_LIBCEF_LIBRARY}")
    endif()
    set(VSGCEF_CEF_RESOURCES_DIR "${VSGCEF_CEF_ROOT}/Resources")
    set(VSGCEF_CEF_LOCALES_DIR "${VSGCEF_CEF_ROOT}/Resources/locales")

    if(EXISTS "${VSGCEF_CEF_INCLUDE_DIR}/cef_version.h" AND
       EXISTS "${VSGCEF_CEF_LIBCEF_LIBRARY}" AND
       EXISTS "${VSGCEF_CEF_WRAPPER_LIBRARY}")
        add_library(vsgCef_libcef SHARED IMPORTED)
        if(WIN32)
            set_target_properties(vsgCef_libcef PROPERTIES
                IMPORTED_IMPLIB "${VSGCEF_CEF_LIBCEF_LIBRARY}"
                IMPORTED_LOCATION "${VSGCEF_CEF_LIBCEF_RUNTIME}"
                INTERFACE_INCLUDE_DIRECTORIES "${VSGCEF_CEF_ROOT}")
        else()
            set_target_properties(vsgCef_libcef PROPERTIES
                IMPORTED_LOCATION "${VSGCEF_CEF_LIBCEF_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${VSGCEF_CEF_ROOT}")
        endif()

        add_library(vsgCef_cef_dll_wrapper STATIC IMPORTED)
        set_target_properties(vsgCef_cef_dll_wrapper PROPERTIES
            IMPORTED_LOCATION "${VSGCEF_CEF_WRAPPER_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${VSGCEF_CEF_ROOT}"
            INTERFACE_COMPILE_DEFINITIONS "USING_CEF_SHARED;WRAPPING_CEF_SHARED")

        add_executable(vsgCef_cef_smoke src/cef_smoke.cpp)
        file(COPY "${VSGCEF_CEF_RESOURCES_DIR}/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
        target_link_libraries(vsgCef_cef_smoke PRIVATE
            vsgCef_cef_dll_wrapper
            vsgCef_libcef
            Threads::Threads)
        if(UNIX AND NOT APPLE)
            target_link_libraries(vsgCef_cef_smoke PRIVATE dl)
        endif()
        if(UNIX AND NOT APPLE)
            target_link_libraries(vsgCef_cef_smoke PRIVATE X11)
        endif()
        if(WIN32)
            target_link_libraries(vsgCef_cef_smoke PRIVATE
                comctl32
                dbghelp
                rpcrt4
                shlwapi
                version
                winmm
                ws2_32)
            if(MSVC)
                target_link_libraries(vsgCef_cef_smoke PRIVATE delayimp)
                target_link_options(vsgCef_cef_smoke PRIVATE /DELAYLOAD:libcef.dll)
            endif()
        endif()
        target_compile_definitions(vsgCef_cef_smoke PRIVATE
            VSGCEF_CEF_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/cef_smoke_cache"
            VSGCEF_CEF_RESOURCES_DIR="${CMAKE_CURRENT_BINARY_DIR}"
            VSGCEF_CEF_LOCALES_DIR="${CMAKE_CURRENT_BINARY_DIR}/locales")
        set(VSGCEF_CEF_RUNTIME_FILES
            v8_context_snapshot.bin
            vk_swiftshader_icd.json)
        if(WIN32)
            list(APPEND VSGCEF_CEF_RUNTIME_FILES
                libcef.dll
                chrome_elf.dll
                d3dcompiler_47.dll
                libEGL.dll
                libGLESv2.dll
                vk_swiftshader.dll
                vulkan-1.dll)
        else()
            list(APPEND VSGCEF_CEF_RUNTIME_FILES
                libcef.so
                libEGL.so
                libGLESv2.so
                libvk_swiftshader.so
                libvulkan.so.1
                chrome-sandbox)
        endif()
        foreach(runtime_file ${VSGCEF_CEF_RUNTIME_FILES})
            if(EXISTS "${VSGCEF_CEF_RUNTIME_DIR}/${runtime_file}")
                add_custom_command(TARGET vsgCef_cef_smoke POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${VSGCEF_CEF_RUNTIME_DIR}/${runtime_file}"
                        "$<TARGET_FILE_DIR:vsgCef_cef_smoke>/${runtime_file}"
                    COMMENT "Linking CEF runtime file ${runtime_file}")
            endif()
        endforeach()
        set_target_properties(vsgCef_cef_smoke PROPERTIES
            INSTALL_RPATH "$ORIGIN"
            BUILD_WITH_INSTALL_RPATH TRUE)
        set(VSGCEF_CEF_AVAILABLE ON)
    else()
        message(WARNING "CEF smoke target disabled. Set VSGCEF_CEF_ROOT and VSGCEF_CEF_WRAPPER_LIBRARY after building ../imguiCef.")
    endif()
endif()

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
find_program(GLSLANG_VALIDATOR_EXECUTABLE NAMES glslangValidator HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)

if(GLSLC_EXECUTABLE)
    set(VSGCEF_SHADER_COMPILER "${GLSLC_EXECUTABLE}")
    set(VSGCEF_SHADER_ARGS "")
elseif(GLSLANG_VALIDATOR_EXECUTABLE)
    set(VSGCEF_SHADER_COMPILER "${GLSLANG_VALIDATOR_EXECUTABLE}")
    set(VSGCEF_SHADER_ARGS -V)
else()
    message(FATAL_ERROR "No shader compiler found. Install Vulkan SDK glslc or glslangValidator.")
endif()

set(VSGCEF_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${VSGCEF_SHADER_OUTPUT_DIR}")

set(VSGCEF_SHADER_SOURCES
    equator_line.vert
    equator_line.frag
)

set(VSGCEF_SHADER_OUTPUTS "")
foreach(shader_src ${VSGCEF_SHADER_SOURCES})
    set(shader_in "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_src}")
    set(shader_out "${VSGCEF_SHADER_OUTPUT_DIR}/${shader_src}.spv")
    list(APPEND VSGCEF_SHADER_OUTPUTS "${shader_out}")

    add_custom_command(
        OUTPUT "${shader_out}"
        COMMAND "${VSGCEF_SHADER_COMPILER}" ${VSGCEF_SHADER_ARGS} "${shader_in}" -o "${shader_out}"
        DEPENDS "${shader_in}"
        VERBATIM
    )
endforeach()

add_custom_target(vsgCef_shaders ALL DEPENDS ${VSGCEF_SHADER_OUTPUTS})

add_executable(vsgCef
    src/vsgthreading/main.cpp
    src/vsgthreading/VsgThreadingApp.cpp
    src/vsgthreading/FrameData.cpp
    src/vsgthreading/Simulator.cpp
    src/vsgthreading/SceneObject.cpp
    src/vsgthreading/StatsUi.cpp
)

target_include_directories(vsgCef PRIVATE src)

target_link_libraries(vsgCef PRIVATE
    vsg::vsg
    vsgImGui::vsgImGui
)

if(VSGCEF_TRACY_AVAILABLE)
    target_link_libraries(vsgCef PRIVATE vsgCef_tracy)
    target_compile_definitions(vsgCef PRIVATE VSGCEF_ENABLE_TRACY)
endif()

if(VSGCEF_CEF_AVAILABLE)
    target_sources(vsgCef PRIVATE src/vsgcef/CefUi.cpp)
    target_link_libraries(vsgCef PRIVATE
        vsgCef_cef_dll_wrapper
        vsgCef_libcef
        Threads::Threads)
    if(UNIX AND NOT APPLE)
        target_link_libraries(vsgCef PRIVATE dl)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(vsgCef PRIVATE X11)
    endif()
    if(WIN32)
        target_link_libraries(vsgCef PRIVATE
            comctl32
            dbghelp
            rpcrt4
            shlwapi
            version
            winmm
            ws2_32)
        if(MSVC)
            target_link_libraries(vsgCef PRIVATE delayimp)
            target_link_options(vsgCef PRIVATE /DELAYLOAD:libcef.dll)
        endif()
    endif()
    target_compile_definitions(vsgCef PRIVATE
        VSGCEF_ENABLE_CEF_RUNTIME
        VSGCEF_CEF_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cef_ui")
    foreach(runtime_file ${VSGCEF_CEF_RUNTIME_FILES})
        if(EXISTS "${VSGCEF_CEF_RUNTIME_DIR}/${runtime_file}")
            add_custom_command(TARGET vsgCef POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${VSGCEF_CEF_RUNTIME_DIR}/${runtime_file}"
                    "$<TARGET_FILE_DIR:vsgCef>/${runtime_file}"
                COMMENT "Linking CEF runtime file ${runtime_file}")
        endif()
    endforeach()
    set_target_properties(vsgCef PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE)
endif()

if(WIN32)
    target_compile_definitions(vsgCef PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

target_compile_definitions(vsgCef PRIVATE VKVSG_SHADER_DIR="${VSGCEF_SHADER_OUTPUT_DIR}")
add_dependencies(vsgCef vsgCef_shaders)
