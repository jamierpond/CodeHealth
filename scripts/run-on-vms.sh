#!/usr/bin/env bash
# Syncs to a Linux and a Windows machine over ssh, then builds, tests and runs
# the CLI on each. Usage: scripts/run-on-vms.sh <linux-host> <windows-host>
# The Linux host needs nix for curl headers; the Windows host needs Visual Studio.
set -euo pipefail
cd "$(dirname "$0")/.."
linuxHost=$1
windowsHost=$2
scripts/sync-to-vm.sh "$linuxHost" linux
ssh "$linuxHost" 'cd projects/CodeHealth && nix-shell -p curl.dev pkg-config --run "scripts/test.sh && build/codehealth . --top 5"'
scripts/sync-to-vm.sh "$windowsHost" windows
ssh "$windowsHost" 'cmd /c "cd projects\CodeHealth && scripts\test-windows.cmd"'
