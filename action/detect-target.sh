#!/usr/bin/env bash
# Prints target=<release target> for this GitHub runner, for $GITHUB_OUTPUT.
set -euo pipefail
case "$RUNNER_OS-$RUNNER_ARCH" in
    macOS-*) target=macos-universal ;;
    Linux-X64) target=linux-x64 ;;
    Linux-ARM64) target=linux-arm64 ;;
    Windows-X64) target=windows-x64 ;;
    Windows-ARM64) target=windows-arm64 ;;
    *) echo "::error title=codehealth::no codehealth build for $RUNNER_OS $RUNNER_ARCH" >&2; exit 1 ;;
esac
echo "target=$target"
