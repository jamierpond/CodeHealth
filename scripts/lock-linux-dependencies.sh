#!/usr/bin/env bash
# Adds the dependencies only Linux builds ask for (eacp's glslang) to lockstep.lock,
# by configuring in update mode inside an Ubuntu container. Needs docker.
set -euo pipefail
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/src -w /src ubuntu:26.04 bash -c '
  apt-get update -qq && apt-get install -y -qq cmake ninja-build g++ git curl libcurl4-gnutls-dev >/dev/null
  git config --global --add safe.directory "*"
  cmake -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLOCKSTEP_MODE=update'
