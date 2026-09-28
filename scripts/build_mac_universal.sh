#!/usr/bin/env bash
# Build a universal (Intel + Apple Silicon) WED.app.
#
# Conan builds most third-party libraries with autotools or their own scripts,
# which cannot produce fat binaries in one pass. So each architecture gets its
# own Conan install and CMake build - one of them cross-compiled on whichever Mac
# runs this - and lipo joins the two executables into one bundle. Everything in
# the bundle but Contents/MacOS/WED is identical between the two builds.
#
# usage: scripts/build_mac_universal.sh            -> build_Universal/WED.app
#   GENERATOR=Ninja|Xcode (default Ninja), IBTOOL=<path> to override ibtool
set -euo pipefail

GENERATOR="${GENERATOR:-Ninja}"
OUT="build_Universal"
IBTOOL_ARG=()
[[ -n "${IBTOOL:-}" ]] && IBTOOL_ARG=(-DIBTOOL="$IBTOOL")

build_arch() {
    local conan_arch="$1" cmake_arch="$2" min_os="$3"
    local dir="build_Release_${cmake_arch}"
    echo "=== ${cmake_arch}: conan"
    # os.version: the libraries must not target a newer macOS than WED does,
    # or the app links with warnings and fails to launch on older systems.
    conan install . --profile default --build=missing \
        --output-folder="$dir" \
        -s:h build_type=Release -s:h arch="$conan_arch" -s:h os.version="$min_os"
    echo "=== ${cmake_arch}: cmake"
    cmake -S . -B "$dir" -G "$GENERATOR" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_CONFIGURATION_TYPES=Release \
        -DCMAKE_OSX_ARCHITECTURES="$cmake_arch" -DCMAKE_OSX_DEPLOYMENT_TARGET="$min_os" \
        -DCMAKE_TOOLCHAIN_FILE="$dir/build/Release/generators/conan_toolchain.cmake" \
        "${IBTOOL_ARG[@]}"
    echo "=== ${cmake_arch}: build"
    cmake --build "$dir" --config Release --target WED
}

# Apple Silicon needs macOS 11 at least; Intel keeps WED's 10.15.
build_arch x86_64 x86_64 10.15
build_arch armv8  arm64  11.0

app() { find "$1" -maxdepth 2 -name WED.app -type d | head -1; }
X86_APP="$(app build_Release_x86_64)"
ARM_APP="$(app build_Release_arm64)"

echo "=== lipo"
rm -rf "$OUT" && mkdir -p "$OUT"
cp -R "$X86_APP" "$OUT/WED.app"
lipo -create "$X86_APP/Contents/MacOS/WED" "$ARM_APP/Contents/MacOS/WED" \
     -output "$OUT/WED.app/Contents/MacOS/WED"
lipo -info "$OUT/WED.app/Contents/MacOS/WED"
