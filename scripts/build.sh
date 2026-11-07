#!/usr/bin/env bash
# Configures and builds codehealth and its tests (Release) into ./build.
set -euo pipefail
cd "$(dirname "$0")/.."
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -B build -S . "${generator[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
