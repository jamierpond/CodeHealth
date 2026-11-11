set windows-shell := ["cmd.exe", "/c"]

binary := if os() == "windows" { "build\\codehealth.exe" } else { "build/codehealth" }

# Show the recipe list by default.
default:
    @just --list

[doc('Configure and build codehealth (Release)')]
[unix]
build:
    scripts/build.sh

[doc('Configure and build codehealth (Release)')]
[windows]
build:
    scripts\build-windows.cmd

[doc('Build, run the tests, then the self-check')]
[unix]
test:
    scripts/test.sh

[doc('Build, run the tests, then the self-check')]
[windows]
test:
    scripts\test-windows.cmd

[doc('Build, then run codehealth (default: this repository)')]
run *args='.': build
    {{binary}} {{args}}

[doc('Build, then install to ~/.local/bin/codehealth')]
[unix]
install: build
    install -d "$HOME/.local/bin" && install -m 755 build/codehealth "$HOME/.local/bin/codehealth"

[doc('Build, then install to ~/.local/bin/codehealth')]
[windows]
install: build
    if not exist "%USERPROFILE%\.local\bin" mkdir "%USERPROFILE%\.local\bin"
    copy /y build\codehealth.exe "%USERPROFILE%\.local\bin\codehealth.exe"
