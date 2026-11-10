@echo off
rem Runs scripts/measure-build.sh inside the MSVC environment for this machine's architecture.
rem Arguments go to the script: a build directory and, optionally, the parallel job count.
setlocal
cd /d "%~dp0.."
set arch=x64
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set arch=arm64
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set vs=%%i
call "%vs%\VC\Auxiliary\Build\vcvarsall.bat" %arch% >nul || exit /b 1
"%ProgramFiles%\Git\bin\bash.exe" scripts/measure-build.sh %* || exit /b 1
