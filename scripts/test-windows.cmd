@echo off
rem Builds, runs every test through ctest, then the self-check.
setlocal
cd /d "%~dp0.."
call scripts\build-windows.cmd CodeHealthTests || exit /b 1
ctest --test-dir build --output-on-failure || exit /b 1
"%ProgramFiles%\Git\bin\bash.exe" scripts/self-check.sh || exit /b 1
