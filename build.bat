@echo off
setlocal enabledelayedexpansion

:: ============================================================
::  GVulkan build script
::  Compiles shaders, builds ddraw.dll, and deploys to Gothic
:: ============================================================

set MSYS=C:\msys64
set MINGW32=%MSYS%\mingw32\bin
set MINGW64=%MSYS%\mingw64\bin
set GLSLANG=%MINGW64%\glslangValidator.exe
set CMAKE=%MINGW32%\cmake.exe
set MAKE=%MINGW32%\mingw32-make.exe

:: MinGW32 must be on PATH for cmake/make to find gcc, as, ld, etc.
set PATH=%MINGW32%;%PATH%

set PROJECT_DIR=%~dp0
set SRC_DIR=%PROJECT_DIR%src
set SHADER_DIR=%SRC_DIR%\shaders
set BUILD_DIR=%PROJECT_DIR%build

set GOTHIC_DIR=C:\Program Files (x86)\Steam\steamapps\common\Gothic II\system

:: ---- parse args ----
set DO_SHADERS=0
set DO_BUILD=0
set DO_DEPLOY=0

if "%1"=="" goto :all
for %%a in (%*) do (
    if /i "%%a"=="shaders"  set DO_SHADERS=1
    if /i "%%a"=="build"    set DO_BUILD=1
    if /i "%%a"=="deploy"   set DO_DEPLOY=1
    if /i "%%a"=="all"      goto :all
    if /i "%%a"=="clean"    goto :clean
)
goto :run

:all
set DO_SHADERS=1
set DO_BUILD=1
set DO_DEPLOY=1
goto :run

:clean
echo [CLEAN] Removing build directory...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
echo Done.
goto :eof

:run

:: ============================================================
::  1. COMPILE SHADERS
:: ============================================================
if %DO_SHADERS%==1 (
    echo.
    echo ========== COMPILING SHADERS ==========
    if not exist "%GLSLANG%" (
        echo ERROR: glslangValidator not found at %GLSLANG%
        echo Install: pacman -S mingw-w64-x86_64-glslang
        exit /b 1
    )

    echo [SHADER] gothic.vert
    "%GLSLANG%" -V -o "%SHADER_DIR%\gothic_vert.spv" "%SHADER_DIR%\gothic.vert"
    if errorlevel 1 ( echo FAILED & exit /b 1 )

    echo [SHADER] gothic.frag
    "%GLSLANG%" -V -o "%SHADER_DIR%\gothic_frag.spv" "%SHADER_DIR%\gothic.frag"
    if errorlevel 1 ( echo FAILED & exit /b 1 )

    echo [SHADER] Generating GothicShaders.h ...
    python "%SHADER_DIR%\spv_to_header.py" "%SHADER_DIR%"
    if errorlevel 1 ( echo FAILED & exit /b 1 )

    echo [SHADER] All shaders compiled.
)

:: ============================================================
::  2. BUILD DLL
:: ============================================================
if %DO_BUILD%==1 (
    echo.
    echo ========== BUILDING ddraw.dll ==========
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

    pushd "%BUILD_DIR%"
    "%CMAKE%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "%PROJECT_DIR%."
    if errorlevel 1 ( echo CMAKE CONFIGURE FAILED & popd & exit /b 1 )

    "%MAKE%" -j%NUMBER_OF_PROCESSORS%
    if errorlevel 1 ( echo BUILD FAILED & popd & exit /b 1 )
    popd

    echo [BUILD] ddraw.dll built successfully.
)

:: ============================================================
::  3. DEPLOY
:: ============================================================
if %DO_DEPLOY%==1 (
    echo.
    echo ========== DEPLOYING ==========
    if not exist "%BUILD_DIR%\ddraw.dll" (
        echo ERROR: ddraw.dll not found. Run build first.
        exit /b 1
    )
    if not exist "%GOTHIC_DIR%\" (
        echo ERROR: Gothic directory not found.
        echo Edit GOTHIC_DIR in this script.
        exit /b 1
    )

    copy /y "%BUILD_DIR%\ddraw.dll" "%GOTHIC_DIR%\ddraw.dll" >nul
    echo [DEPLOY] ddraw.dll copied to Gothic system dir
    if not exist "%GOTHIC_DIR%\GVulkan\" mkdir "%GOTHIC_DIR%\GVulkan"
    copy /y "%PROJECT_DIR%timecycle.cfg" "%GOTHIC_DIR%\GVulkan\timecycle.cfg" >nul
    echo [DEPLOY] timecycle.cfg copied to Gothic system/GVulkan dir
)

echo.
echo ========== DONE ==========
