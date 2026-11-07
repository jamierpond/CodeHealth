#!/usr/bin/env bash
# Builds, then runs every test through ctest.
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/build.sh
ctest --test-dir build --build-config Release --output-on-failure
scripts/self-check.sh
