# ( [ -f build/CMakeCache.txt ] || cmake -Bbuild ) && cmake --build build
# build/src/audioid

cmake_minimum_required(VERSION 3.10)

project(audioid)

add_subdirectory(src)
