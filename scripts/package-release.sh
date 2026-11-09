#!/usr/bin/env bash
# Packs the built codehealth into dist/codehealth-<target>.tar.gz (.zip on Windows)
# with its SHA-256 in dist/codehealth-<target>.sha256. Fails if the binary's version
# does not match the tag being released ($GITHUB_REF_NAME, when set).
# Usage: package-release.sh <target>, e.g. linux-x64, macos-universal, windows-arm64
set -euo pipefail
cd "$(dirname "$0")/.."
target="$1"
binary=build/codehealth
[ -x build/codehealth.exe ] && binary=build/codehealth.exe
version="$("$binary" --version | tr -d '\r' | cut -d' ' -f2)"
if [ -n "${GITHUB_REF_NAME:-}" ] && [ "$GITHUB_REF_NAME" != "v$version" ]; then
    echo "tag $GITHUB_REF_NAME does not match codehealth $version (CMakeLists.txt project VERSION)" >&2
    exit 1
fi
name="codehealth-$target"
rm -rf dist && mkdir -p "dist/$name"
cp "$binary" LICENSE README.md "dist/$name/"
cd dist
if [ "${binary##*.}" = exe ]; then
    archive="$name.zip"
    powershell -NoProfile -Command "Compress-Archive -Path '$name/*' -DestinationPath '$archive'"
else
    archive="$name.tar.gz"
    tar czf "$archive" -C "$name" .
fi
if command -v sha256sum >/dev/null; then
    sha256sum "$archive" > "$name.sha256"
else
    shasum -a 256 "$archive" > "$name.sha256"
fi
cat "$name.sha256"
