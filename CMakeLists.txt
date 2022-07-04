# [ "$(basename $(pwd))" != "build" ] && ( [ -d build ] || mkdir build ) && cd build; cmake ..
# [ "$(basename $(pwd))" != "build" ] && cd build; cmake --build .
# [ "$(basename $(pwd))" != "build" ] && cd build; src/audioid

cmake_minimum_required(VERSION 3.10)

project(audioid)

add_subdirectory(src)
