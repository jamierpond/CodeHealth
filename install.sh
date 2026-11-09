#!/bin/sh
# Installs the codehealth release binary for this machine (macOS, Linux x64 or arm64).
#   curl -fsSL https://raw.githubusercontent.com/jamierpond/CodeHealth/main/install.sh | sh
# CODEHEALTH_VERSION picks a release (e.g. v0.1.0; default: the latest).
# CODEHEALTH_INSTALL_DIR picks where it goes (default: ~/.local/bin).
set -eu

repository="https://github.com/jamierpond/CodeHealth"
installDir="${CODEHEALTH_INSTALL_DIR:-$HOME/.local/bin}"

fail() {
    echo "codehealth install: $1" >&2
    exit 1
}

detectTarget() {
    case "$(uname -s)" in
        Darwin) echo macos-universal ;;
        Linux)
            case "$(uname -m)" in
                x86_64 | amd64) echo linux-x64 ;;
                aarch64 | arm64) echo linux-arm64 ;;
                *) fail "no Linux build for $(uname -m)" ;;
            esac ;;
        *) fail "no build for $(uname -s); on Windows use install.ps1" ;;
    esac
}

releaseUrl() {
    if [ -n "${CODEHEALTH_VERSION:-}" ]; then
        echo "$repository/releases/download/$CODEHEALTH_VERSION"
    else
        echo "$repository/releases/latest/download"
    fi
}

download() {
    curl -fsSL -o "$2" "$1" || fail "could not download $1"
}

computeSha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

target="$(detectTarget)"
archive="codehealth-$target.tar.gz"
workDir="$(mktemp -d)"
trap 'rm -rf "$workDir"' EXIT

download "$(releaseUrl)/$archive" "$workDir/$archive"
download "$(releaseUrl)/codehealth-$target.sha256" "$workDir/expected.sha256"
expected="$(cut -d' ' -f1 "$workDir/expected.sha256")"
[ "$(computeSha256 "$workDir/$archive")" = "$expected" ] || fail "checksum mismatch for $archive"

tar xzf "$workDir/$archive" -C "$workDir"
mkdir -p "$installDir"
install -m 755 "$workDir/codehealth" "$installDir/codehealth"
echo "installed $("$installDir/codehealth" --version) to $installDir/codehealth"

case ":$PATH:" in
    *":$installDir:"*) ;;
    *) echo "add $installDir to your PATH to run codehealth" ;;
esac
