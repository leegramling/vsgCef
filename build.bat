@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "CONFIG=Release"
set "BUILD_DIR=build-windows"
set "CEF_BUILD_DIR="
set "CEF_RUNTIME_LIBRARY_FLAG=/MD"
set "AUTO_CEF_WRAPPER=OFF"
set "ENABLE_CEF=ON"
set "ENABLE_TRACY=OFF"
set "GENERATOR="

:parse_args
if "%~1"=="" goto after_args
if /I "%~1"=="Debug" set "CONFIG=Debug" & shift & goto parse_args
if /I "%~1"=="Release" set "CONFIG=Release" & shift & goto parse_args
if /I "%~1"=="RelWithDebInfo" set "CONFIG=RelWithDebInfo" & shift & goto parse_args
if /I "%~1"=="--tracy" set "ENABLE_TRACY=ON" & shift & goto parse_args
if /I "%~1"=="--build-dir" set "BUILD_DIR=%~2" & shift & shift & goto parse_args
if /I "%~1"=="--cef-build-dir" set "CEF_BUILD_DIR=%~2" & shift & shift & goto parse_args
if /I "%~1"=="--generator" set "GENERATOR=%~2" & shift & shift & goto parse_args
if /I "%~1"=="--help" goto usage
echo Unknown argument: %~1
goto usage

:after_args
if /I "%CONFIG%"=="Debug" set "CEF_RUNTIME_LIBRARY_FLAG=/MDd"

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake was not found on PATH.
    exit /b 1
)

if not defined VSG_DEPS_INSTALL_DIR (
    set "VSG_DEPS_INSTALL_DIR=%ROOT%\external\vsg_deps\install"
)

if not exist "%VSG_DEPS_INSTALL_DIR%\lib\cmake\vsg\vsgConfig.cmake" (
    echo ERROR: vsgConfig.cmake was not found.
    echo.
    echo Expected:
    echo   %VSG_DEPS_INSTALL_DIR%\lib\cmake\vsg\vsgConfig.cmake
    echo.
    echo Set VSG_DEPS_INSTALL_DIR to your Windows vsg/vsgImGui install prefix, for example:
    echo   set VSG_DEPS_INSTALL_DIR=C:\Users\leegr\dev\vsg_deps\install
    echo.
    echo The install prefix should contain:
    echo   lib\cmake\vsg\vsgConfig.cmake
    echo   lib\cmake\vsgImGui\vsgImGuiConfig.cmake
    exit /b 1
)

if not exist "%VSG_DEPS_INSTALL_DIR%\lib\cmake\vsgImGui\vsgImGuiConfig.cmake" (
    echo ERROR: vsgImGuiConfig.cmake was not found.
    echo.
    echo Expected:
    echo   %VSG_DEPS_INSTALL_DIR%\lib\cmake\vsgImGui\vsgImGuiConfig.cmake
    echo.
    echo Set VSG_DEPS_INSTALL_DIR to your Windows vsg/vsgImGui install prefix.
    exit /b 1
)

if "%ENABLE_CEF%"=="ON" (
    if not defined VSGCEF_CEF_ROOT (
        echo ERROR: VSGCEF_CEF_ROOT is not set.
        echo.
        echo Set it to your Windows CEF binary distribution root, for example:
        echo   set VSGCEF_CEF_ROOT=C:\dev\cef_binary_143.0.14_windows64
        echo.
        exit /b 1
    )

    if not defined VSGCEF_CEF_RUNTIME_DIR (
        set "VSGCEF_CEF_RUNTIME_DIR=%VSGCEF_CEF_ROOT%\%CONFIG%"
        if not exist "!VSGCEF_CEF_RUNTIME_DIR!\libcef.dll" set "VSGCEF_CEF_RUNTIME_DIR=%VSGCEF_CEF_ROOT%\Release"
    )

    if not defined VSGCEF_CEF_WRAPPER_LIBRARY (
        if not defined CEF_BUILD_DIR set "CEF_BUILD_DIR=%VSGCEF_CEF_ROOT%\build"
        set "AUTO_CEF_WRAPPER=ON"
        set "VSGCEF_CEF_WRAPPER_LIBRARY=!CEF_BUILD_DIR!\libcef_dll_wrapper\%CONFIG%\libcef_dll_wrapper.lib"
        if not exist "!VSGCEF_CEF_WRAPPER_LIBRARY!" set "VSGCEF_CEF_WRAPPER_LIBRARY=%VSGCEF_CEF_ROOT%\build\libcef_dll_wrapper\libcef_dll_wrapper.lib"
    )

    if not exist "!VSGCEF_CEF_ROOT!\include\cef_version.h" (
        echo ERROR: CEF headers were not found under "!VSGCEF_CEF_ROOT!\include".
        exit /b 1
    )
    if not exist "!VSGCEF_CEF_RUNTIME_DIR!\libcef.dll" (
        echo ERROR: libcef.dll was not found in "!VSGCEF_CEF_RUNTIME_DIR!".
        exit /b 1
    )
    if not exist "!VSGCEF_CEF_RUNTIME_DIR!\libcef.lib" (
        echo ERROR: libcef.lib was not found in "!VSGCEF_CEF_RUNTIME_DIR!".
        exit /b 1
    )
    if "!AUTO_CEF_WRAPPER!"=="ON" (
        if not defined CEF_BUILD_DIR set "CEF_BUILD_DIR=%VSGCEF_CEF_ROOT%\build"
        echo Building/updating CEF wrapper with %CEF_RUNTIME_LIBRARY_FLAG% runtime...
        if defined GENERATOR (
            cmake -S "!VSGCEF_CEF_ROOT!" -B "!CEF_BUILD_DIR!" -G "%GENERATOR%" -DCEF_RUNTIME_LIBRARY_FLAG=%CEF_RUNTIME_LIBRARY_FLAG%
        ) else (
            cmake -S "!VSGCEF_CEF_ROOT!" -B "!CEF_BUILD_DIR!" -DCEF_RUNTIME_LIBRARY_FLAG=%CEF_RUNTIME_LIBRARY_FLAG%
        )
        if errorlevel 1 exit /b 1

        cmake --build "!CEF_BUILD_DIR!" --config %CONFIG% --target libcef_dll_wrapper
        if errorlevel 1 exit /b 1

        set "VSGCEF_CEF_WRAPPER_LIBRARY=!CEF_BUILD_DIR!\libcef_dll_wrapper\%CONFIG%\libcef_dll_wrapper.lib"
        if not exist "!VSGCEF_CEF_WRAPPER_LIBRARY!" set "VSGCEF_CEF_WRAPPER_LIBRARY=!CEF_BUILD_DIR!\libcef_dll_wrapper\libcef_dll_wrapper.lib"
        if not exist "!VSGCEF_CEF_WRAPPER_LIBRARY!" set "VSGCEF_CEF_WRAPPER_LIBRARY=!CEF_BUILD_DIR!\%CONFIG%\libcef_dll_wrapper.lib"
        if not exist "!VSGCEF_CEF_WRAPPER_LIBRARY!" set "VSGCEF_CEF_WRAPPER_LIBRARY=!CEF_BUILD_DIR!\libcef_dll_wrapper.lib"
    )

    if not exist "!VSGCEF_CEF_WRAPPER_LIBRARY!" (
        if not defined CEF_BUILD_DIR set "CEF_BUILD_DIR=%VSGCEF_CEF_ROOT%\build"
        echo ERROR: libcef_dll_wrapper.lib was not found.
        echo.
        if "!AUTO_CEF_WRAPPER!"=="OFF" (
            echo VSGCEF_CEF_WRAPPER_LIBRARY was set explicitly, so build.bat did not rebuild it.
        )
        if not exist "!VSGCEF_CEF_WRAPPER_LIBRARY!" (
            echo Looked in:
            echo   !CEF_BUILD_DIR!\libcef_dll_wrapper\%CONFIG%\libcef_dll_wrapper.lib
            echo   !CEF_BUILD_DIR!\libcef_dll_wrapper\libcef_dll_wrapper.lib
            echo   !CEF_BUILD_DIR!\%CONFIG%\libcef_dll_wrapper.lib
            echo   !CEF_BUILD_DIR!\libcef_dll_wrapper.lib
            echo Set VSGCEF_CEF_WRAPPER_LIBRARY to the actual .lib path and rerun build.bat.
            exit /b 1
        )
    )
)

if not exist "%ROOT%\%BUILD_DIR%" mkdir "%ROOT%\%BUILD_DIR%"
set "CACHE_FILE=%ROOT%\%BUILD_DIR%\vsgcef-windows-cache.cmake"
set "VSG_DEPS_INSTALL_DIR_CMAKE=%VSG_DEPS_INSTALL_DIR:\=/%"
set "VSGCEF_CEF_ROOT_CMAKE=%VSGCEF_CEF_ROOT:\=/%"
set "VSGCEF_CEF_RUNTIME_DIR_CMAKE=%VSGCEF_CEF_RUNTIME_DIR:\=/%"
set "VSGCEF_CEF_WRAPPER_LIBRARY_CMAKE=%VSGCEF_CEF_WRAPPER_LIBRARY:\=/%"
set "VSGCEF_TRACY_ROOT_CMAKE=%VSGCEF_TRACY_ROOT:\=/%"

> "%CACHE_FILE%" echo set^(CMAKE_BUILD_TYPE "%CONFIG%" CACHE STRING "" FORCE^)
>> "%CACHE_FILE%" echo set^(VSG_DEPS_INSTALL_DIR "%VSG_DEPS_INSTALL_DIR_CMAKE%" CACHE PATH "" FORCE^)
>> "%CACHE_FILE%" echo set^(VSGCEF_ENABLE_CEF "%ENABLE_CEF%" CACHE BOOL "" FORCE^)
>> "%CACHE_FILE%" echo set^(VSGCEF_ENABLE_TRACY "%ENABLE_TRACY%" CACHE BOOL "" FORCE^)
if "%ENABLE_CEF%"=="ON" (
    >> "%CACHE_FILE%" echo set^(VSGCEF_CEF_ROOT "%VSGCEF_CEF_ROOT_CMAKE%" CACHE PATH "" FORCE^)
    >> "%CACHE_FILE%" echo set^(VSGCEF_CEF_RUNTIME_DIR "%VSGCEF_CEF_RUNTIME_DIR_CMAKE%" CACHE PATH "" FORCE^)
    >> "%CACHE_FILE%" echo set^(VSGCEF_CEF_WRAPPER_LIBRARY "%VSGCEF_CEF_WRAPPER_LIBRARY_CMAKE%" CACHE FILEPATH "" FORCE^)
)
if "%ENABLE_TRACY%"=="ON" if defined VSGCEF_TRACY_ROOT (
    >> "%CACHE_FILE%" echo set^(VSGCEF_TRACY_ROOT "%VSGCEF_TRACY_ROOT_CMAKE%" CACHE PATH "" FORCE^)
)

echo Configuring vsgCef...
if defined GENERATOR (
    cmake -S "%ROOT%" -B "%ROOT%\%BUILD_DIR%" -G "%GENERATOR%" -C "%CACHE_FILE%"
) else (
    cmake -S "%ROOT%" -B "%ROOT%\%BUILD_DIR%" -C "%CACHE_FILE%"
)
if errorlevel 1 exit /b 1

echo Building vsgCef %CONFIG%...
cmake --build "%ROOT%\%BUILD_DIR%" --config %CONFIG% --target vsgCef
if errorlevel 1 exit /b 1

echo.
echo Build complete.
echo Executable output is under:
echo   %ROOT%\%BUILD_DIR%
echo.
echo If using a multi-config generator such as Visual Studio, check:
echo   %ROOT%\%BUILD_DIR%\%CONFIG%
exit /b 0

:usage
echo Usage:
echo   build.bat [Release^|Debug^|RelWithDebInfo] [--tracy] [--build-dir DIR] [--cef-build-dir DIR] [--generator NAME]
echo.
echo Environment variables:
echo   VSG_DEPS_INSTALL_DIR            Path to installed vsg/vsgImGui prefix.
echo   VSGCEF_CEF_ROOT                 Path to Windows CEF binary distribution root.
echo   VSGCEF_CEF_RUNTIME_DIR          Usually %%VSGCEF_CEF_ROOT%%\Release.
echo   VSGCEF_CEF_WRAPPER_LIBRARY      Path to libcef_dll_wrapper.lib.
echo   VSGCEF_TRACY_ROOT               Optional Tracy source root when using --tracy.
echo.
echo If VSGCEF_CEF_WRAPPER_LIBRARY is not set and the wrapper .lib is missing,
echo this script configures VSGCEF_CEF_ROOT with CMake and builds libcef_dll_wrapper.
exit /b 1
