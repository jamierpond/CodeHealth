@echo off
rem Configures and builds codehealth and its tests (Release) with MSVC for this machine's architecture.
setlocal
cd /d "%~dp0.."
set arch=x64
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set arch=arm64
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set vs=%%i
call "%vs%\VC\Auxiliary\Build\vcvarsall.bat" %arch% >nul || exit /b 1
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --parallel || exit /b 1
