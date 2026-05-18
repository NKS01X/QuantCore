#!/bin/bash
# Use CMake downloaded by vcpkg since it requires a newer version than some default package managers
CMAKE_EXEC="./vcpkg/downloads/tools/cmake-4.3.2-linux/cmake-4.3.2-linux-x86_64/bin/cmake"

if [ ! -f "$CMAKE_EXEC" ]; then
    # Fallback to system cmake
    CMAKE_EXEC="cmake"
fi

$CMAKE_EXEC -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
$CMAKE_EXEC --build build
