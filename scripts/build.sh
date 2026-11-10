#!/usr/bin/env bash
# Configures and builds codehealth (Release) into ./build; the tests are left to scripts/test.sh.
# Extra arguments go to CMake's configure step, e.g. -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".
set -euo pipefail
cd "$(dirname "$0")/.."
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -B build -S . "${generator[@]}" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --config Release --parallel --target codehealth
