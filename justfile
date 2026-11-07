# CodeHealth — build / test / run / install. Recipes call the scripts in scripts/.
#
#   just build            configure + build (Release)
#   just test             build, run the tests, then the self-check
#   just run [args]       build, then run codehealth (default: this repository)
#   just install          build, then install to ~/.local/bin/codehealth

set windows-shell := ["cmd.exe", "/c"]

binary := if os() == "windows" { "build\\codehealth.exe" } else { "build/codehealth" }

# Default: show the recipe list.
default:
    @just --list

# Configure and build (Release).
[unix]
build:
    scripts/build.sh

# Configure and build (Release).
[windows]
build:
    scripts\build-windows.cmd

# Build, run the tests, then the self-check.
[unix]
test:
    scripts/test.sh

# Build, run the tests, then the self-check.
[windows]
test:
    scripts\test-windows.cmd

# Build, then run codehealth (default: this repository).
run *args='.': build
    {{binary}} {{args}}

# Build, then install to ~/.local/bin/codehealth.
[unix]
install: build
    install -d "$HOME/.local/bin" && install -m 755 build/codehealth "$HOME/.local/bin/codehealth"

# Build, then install to ~/.local/bin/codehealth.
[windows]
install: build
    if not exist "%USERPROFILE%\.local\bin" mkdir "%USERPROFILE%\.local\bin"
    copy /y build\codehealth.exe "%USERPROFILE%\.local\bin\codehealth.exe"
