@echo off
rem Builds and tests with MSVC for this machine's architecture, then runs the CLI on this repo.
setlocal
cd /d "%~dp0.."
set arch=x64
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set arch=arm64
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set vs=%%i
call "%vs%\VC\Auxiliary\Build\vcvarsall.bat" %arch% >nul || exit /b 1
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --parallel || exit /b 1
ctest --test-dir build --output-on-failure || exit /b 1
"%ProgramFiles%\Git\bin\bash.exe" scripts/self-check.sh || exit /b 1
build\codehealth.exe . --top 5
