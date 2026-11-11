#!/usr/bin/env bash
# Builds codehealth and its tests, runs every test through ctest, then the self-check.
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/build.sh
cmake --build build --config Release --parallel --target CodeHealthTests
ctest --test-dir build --build-config Release --output-on-failure
node scripts/self-check.mjs
