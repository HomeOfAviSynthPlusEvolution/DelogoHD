@echo off

mkdir build\msvc-x86
pushd build\msvc-x86
cmake -DCMAKE_GENERATOR_PLATFORM=Win32 -D_DIR=msvc-x86 ..\..\
popd
mkdir build\msvc-x64
pushd build\msvc-x64
cmake -DCMAKE_GENERATOR_PLATFORM=x64 -D_DIR=msvc-x64 ..\..\
popd
cmake --build build\msvc-x86 --config Release
cmake --build build\msvc-x64 --config Release

mkdir build\clang-x86
pushd build\clang-x86
cmake -TClangCL -DCMAKE_GENERATOR_PLATFORM=Win32 -D_DIR=clang-x86 ..\..\
popd
mkdir build\clang-x64
pushd build\clang-x64
cmake -TClangCL -DCMAKE_GENERATOR_PLATFORM=x64 -D_DIR=clang-x64 ..\..\
popd
cmake --build build\clang-x86 --config Release
cmake --build build\clang-x64 --config Release

mkdir build\gcc-x64
pushd build\gcc-x64
cmake -G "MSYS Makefiles" -DCMAKE_CXX_COMPILER=D:/msys64/mingw64/bin/g++.exe -DCMAKE_CXX_FLAGS=-msse4.1 -D_DIR=gcc-x64 ..\..\
popd
cmake --build build\gcc-x64 --config Release
