#!/usr/bin/env bash
# Downloads a codehealth release for one target into a directory and checks its SHA-256.
# Usage: download.sh <version tag> <target> <directory>
set -euo pipefail
version="$1" target="$2" directory="$3"
url="https://github.com/jamierpond/CodeHealth/releases/download/$version"
extension=tar.gz
[[ "$target" == windows-* ]] && extension=zip
archive="codehealth-$target.$extension"
mkdir -p "$directory"
cd "$directory"
curl -fsSL --retry 3 -o "$archive" "$url/$archive"
curl -fsSL --retry 3 -o expected.sha256 "$url/codehealth-$target.sha256"
expected="$(cut -d' ' -f1 expected.sha256)"
if command -v sha256sum >/dev/null; then
    actual="$(sha256sum "$archive" | cut -d' ' -f1)"
else
    actual="$(shasum -a 256 "$archive" | cut -d' ' -f1)"
fi
if [ "$actual" != "$expected" ]; then
    echo "::error title=codehealth::checksum mismatch for $archive" >&2
    exit 1
fi
if [ "$extension" = zip ]; then unzip -oq "$archive"; else tar xzf "$archive"; fi
rm -f "$archive" expected.sha256
