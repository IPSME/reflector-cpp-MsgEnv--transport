@echo off
rem clean - remove all CMake-generated project files (Visual Studio / Xcode / Makefiles),
rem build outputs and caches, leaving only the base source + CMakeLists.txt + README.
rem
rem Usage:  clean.cmd        (Windows)
cd /d "%~dp0"

for %%d in (build out) do if exist "%%d" rmdir /s /q "%%d"
for /d %%d in (build-*) do rmdir /s /q "%%d"
for %%d in (CMakeFiles _deps Debug Release x64 .vs _DerivedData) do if exist "%%d" rmdir /s /q "%%d"
del /q CMakeCache.txt cmake_install.cmake CTestTestfile.cmake compile_commands.json 2>nul
del /q *.sln *.vcxproj *.vcxproj.filters *.vcxproj.user 2>nul

echo cleaned: only base source remains
