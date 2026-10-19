# solidcraft-occt-provider: SolidCraft's geometry provider program backed by Open CASCADE
# Technology. Built against an installed OCCT (this repository's pinned release):
#
#   cmake -S solidcraft-provider -B build/provider -DOpenCASCADE_DIR=<occt install>/lib/cmake/opencascade
#   cmake --build build/provider
#
# See README.md (and build.sh for building OCCT and the provider together).

cmake_minimum_required(VERSION 3.16)
project(solidcraft-occt-provider LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCASCADE REQUIRED)

add_executable(solidcraft-occt-provider src/main.cpp src/json.cpp)
target_include_directories(solidcraft-occt-provider PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(solidcraft-occt-provider PRIVATE TKernel TKMath TKG3d TKBRep TKGeomBase TKTopAlgo TKPrim TKBO TKShHealing TKXSBase TKDESTEP)

# Find OCCT's shared libraries where they were installed, without setting a library path.
set_target_properties(solidcraft-occt-provider PROPERTIES BUILD_RPATH "${OpenCASCADE_LIBRARY_DIR}" INSTALL_RPATH "${OpenCASCADE_LIBRARY_DIR}")

install(TARGETS solidcraft-occt-provider RUNTIME DESTINATION bin)
