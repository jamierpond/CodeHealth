@echo off
rem Builds, runs every test through ctest, then the self-check.
setlocal
cd /d "%~dp0.."
call scripts\build-windows.cmd CodeHealthTests || exit /b 1
ctest --test-dir build --output-on-failure || exit /b 1
node scripts\self-check.mjs || exit /b 1
