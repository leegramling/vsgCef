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

if(WIN32)
    target_compile_definitions(vsgCef PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

target_compile_definitions(vsgCef PRIVATE VKVSG_SHADER_DIR="${VSGCEF_SHADER_OUTPUT_DIR}")
add_dependencies(vsgCef vsgCef_shaders)
