# CodeHealth dev commands. Recipes are a line or two of cmake; anything longer is
# a script in scripts/.

set windows-shell := ["cmd.exe", "/c"]

build_dir := "build"
build_type := "Release"
binary := if os() == "windows" { build_dir + "\\codehealth.exe" } else { build_dir + "/codehealth" }

# Ninja drives cl directly on Windows, so it needs the Visual Studio environment.
msvc := if os() == "windows" { "powershell -NoProfile -ExecutionPolicy Bypass -File scripts/with-msvc.ps1" } else { "" }

# Show the recipe list by default.
default:
    @just --list

[doc('Configure + build codehealth; extra arguments go to the configure step')]
build *cmake_args:
    {{msvc}} cmake -S . -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE={{build_type}} {{cmake_args}}
    {{msvc}} cmake --build {{build_dir}} --target codehealth

[doc('Build codehealth and its tests, run every test, then the self-check')]
test: build
    {{msvc}} cmake --build {{build_dir}} --target CodeHealthTests
    ctest --test-dir {{build_dir}} --output-on-failure
    node scripts/self-check.mjs

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
