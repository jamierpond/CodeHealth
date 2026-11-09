#!/usr/bin/env bash
# Runs the published installers from main on this machine and on the Parallels VMs
# (ubuntu-vm, windows-vm over ssh) into a scratch directory, then runs
# `codehealth --version` from it. Usage: verify-installers.sh [hosts...]
set -euo pipefail
raw="https://raw.githubusercontent.com/jamierpond/CodeHealth/main"

verifyHere() {
    local directory
    directory="$(mktemp -d)"
    curl -fsSL "$raw/install.sh" | CODEHEALTH_INSTALL_DIR="$directory" sh
    "$directory/codehealth" --version
    rm -rf "$directory"
}

verifyUnixHost() {
    ssh "$1" "set -e; d=\$(mktemp -d); curl -fsSL $raw/install.sh | CODEHEALTH_INSTALL_DIR=\$d sh; \$d/codehealth --version; rm -rf \$d"
}

verifyWindowsHost() {
    ssh "$1" "powershell -NoProfile -Command \"\$env:CODEHEALTH_INSTALL_DIR = Join-Path \$env:TEMP 'codehealth-verify'; \$env:CODEHEALTH_NO_MODIFY_PATH = '1'; irm $raw/install.ps1 | iex; & (Join-Path \$env:CODEHEALTH_INSTALL_DIR 'codehealth.exe') --version; Remove-Item -Recurse -Force \$env:CODEHEALTH_INSTALL_DIR\""
}

hosts=("$@")
[ ${#hosts[@]} -eq 0 ] && hosts=(local ubuntu-vm windows-vm)
for host in "${hosts[@]}"; do
    echo "== $host"
    case "$host" in
        local) verifyHere ;;
        windows*) verifyWindowsHost "$host" ;;
        *) verifyUnixHost "$host" ;;
    esac
done
