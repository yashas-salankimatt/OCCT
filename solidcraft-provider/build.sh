#!/bin/sh
# Build this repository's Open CASCADE Technology (geometry, algorithms and STEP only) and
# solidcraft-occt-provider against it.
#
#   solidcraft-provider/build.sh [BUILD_DIR]     (default: ../occt-build beside this repository)
#
# The provider ends up at BUILD_DIR/install/bin/solidcraft-occt-provider; point SolidCraft at it:
#   SOLIDCRAFT_GEOMETRY_PROVIDER=BUILD_DIR/install/bin/solidcraft-occt-provider solidcraft-cli mcp
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
build=${1:-"$repo/../occt-build"}
mkdir -p "$build"
build=$(cd "$build" && pwd)

generator=""
if command -v ninja >/dev/null 2>&1; then generator="-G Ninja"; fi

# shellcheck disable=SC2086
cmake -S "$repo" -B "$build/occt" $generator -DCMAKE_BUILD_TYPE=Release -DBUILD_LIBRARY_TYPE=Shared \
  -DINSTALL_DIR="$build/install" \
  -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_Visualization=OFF \
  -DUSE_FREETYPE=OFF -DUSE_OPENGL=OFF -DUSE_TK=OFF -DUSE_TCL=OFF
cmake --build "$build/occt" --target install

# shellcheck disable=SC2086
cmake -S "$here" -B "$build/provider" $generator -DCMAKE_BUILD_TYPE=Release \
  -DOpenCASCADE_DIR="$build/install/lib/cmake/opencascade" -DCMAKE_INSTALL_PREFIX="$build/install"
cmake --build "$build/provider" --target install

"$build/install/bin/solidcraft-occt-provider" --self-test
echo "built $build/install/bin/solidcraft-occt-provider"
