@echo off
rem quietkey build script.
rem
rem NOTE 1: keep this file ASCII-only. Batch files are read using the console's
rem   OEM code page, so non-ASCII text here gets mangled and can even break
rem   command parsing. Chinese docs live in README.md instead.
rem NOTE 2: Visual Studio lives under "C:\Program Files (x86)\...". Those
rem   parentheses close a FOR/IF block early if the value is expanded at parse
rem   time, so every VS path is expanded with !delayed! syntax and quoted.
rem
rem   build.bat          Release build -> build\quietkey.exe
rem   build.bat debug    Debug build
rem   build.bat clean    remove the build directory

setlocal EnableDelayedExpansion

set "CFG=Release"
if /i "%~1"=="debug" set "CFG=Debug"
if /i "%~1"=="clean" goto :clean

rem --- locate Visual Studio ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" goto :no_vs

rem Write to a temp file instead of using FOR /F with backticks: the VS path
rem contains spaces and parentheses, which makes the backtick form fragile.
set "VSTMP=%TEMP%\quietkey_vspath.txt"
"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "!VSTMP!" 2>nul
set "VSDIR="
if exist "!VSTMP!" set /p VSDIR=<"!VSTMP!"
del "!VSTMP!" >nul 2>&1
if not defined VSDIR goto :no_msvc

set "CMAKE=!VSDIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=!VSDIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "!CMAKE!" set "CMAKE=cmake"

rem --- enter the x64 build environment ---
rem stderr is silenced on purpose: vcvars64.bat itself prints a bogus
rem "'vswhere.exe' is not recognized" line on this machine while still setting
rem the environment correctly. Failures are still caught by the errorlevel check.
call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 goto :no_vcvars

rem --- configure ---
if exist "!NINJA!" goto :cfg_ninja
"!CMAKE!" -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=!CFG!
goto :cfg_done
:cfg_ninja
"!CMAKE!" -S "%~dp0." -B "%~dp0build" -G Ninja "-DCMAKE_MAKE_PROGRAM=!NINJA!" -DCMAKE_BUILD_TYPE=!CFG!
:cfg_done
if errorlevel 1 exit /b 1

rem --- build ---
"!CMAKE!" --build "%~dp0build"
if errorlevel 1 exit /b 1

echo.
echo Build OK: %~dp0build\quietkey.exe  [!CFG!]
exit /b 0

:clean
if exist "%~dp0build" rmdir /s /q "%~dp0build"
echo Cleaned.
exit /b 0

:no_vs
echo [ERROR] vswhere.exe not found - Visual Studio Build Tools are not installed.
echo         Install "Visual Studio Build Tools" with the C++ desktop workload.
exit /b 1

:no_msvc
echo [ERROR] No MSVC toolset found in the Visual Studio installation.
exit /b 1

:no_vcvars
echo [ERROR] vcvars64.bat failed.
exit /b 1
