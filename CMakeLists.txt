# [ -d build ] || mkdir build
# [ "$(basename $(pwd))" != "build" ] && cd build; cmake .. 
# [ "$(basename $(pwd))" != "build" ] && cd build; cmake --build .

cmake_minimum_required(VERSION 3.10)

project(SoundId)

add_subdirectory(src)
