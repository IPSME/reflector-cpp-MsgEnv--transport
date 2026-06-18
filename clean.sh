#!/bin/sh
# clean — remove all CMake-generated project files (Xcode / Visual Studio / Makefiles),
# build outputs and caches, leaving only the base source + CMakeLists.txt + README.
#
# Usage:  ./clean.sh        (macOS / Linux / Git-Bash)
cd "$(dirname "$0")" || exit 1

# out-of-source build trees (normal flow: `cmake -B build ...` puts everything here)
rm -rf build build-* out

# in-source leftovers, in case cmake was ever configured in the project root
rm -rf CMakeFiles _deps Debug Release x64 .vs _DerivedData
rm -rf -- *.xcodeproj
rm -f  CMakeCache.txt cmake_install.cmake CTestTestfile.cmake compile_commands.json
rm -f  -- *.sln *.vcxproj *.vcxproj.filters *.vcxproj.user

echo "cleaned: only base source remains"
