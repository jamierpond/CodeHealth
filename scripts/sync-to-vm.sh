#!/usr/bin/env bash
# Copies the committed history to a VM through a git bundle and checks out HEAD
# at ~/projects/CodeHealth there. Usage: scripts/sync-to-vm.sh <host> [linux|windows]
set -euo pipefail
cd "$(dirname "$0")/.."
host=$1
system=${2:-linux}
bundle=$(mktemp -d)/CodeHealth.bundle
git bundle create "$bundle" HEAD --branches
scp -q "$bundle" "$host:CodeHealth.bundle"
if [ "$system" = windows ]; then
    ssh "$host" 'powershell -NoProfile -Command "New-Item -ItemType Directory -Force projects | Out-Null; if (!(Test-Path projects/CodeHealth)) { git clone -q CodeHealth.bundle projects/CodeHealth }; git -C projects/CodeHealth fetch -q ../../CodeHealth.bundle HEAD; git -C projects/CodeHealth reset -q --hard FETCH_HEAD"'
else
    ssh "$host" 'mkdir -p projects && { [ -d projects/CodeHealth ] || git clone -q CodeHealth.bundle projects/CodeHealth; } && cd projects/CodeHealth && git fetch -q ~/CodeHealth.bundle HEAD && git reset -q --hard FETCH_HEAD'
fi
