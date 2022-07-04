cmake_minimum_required(VERSION 3.10)

add_executable(SoundId
	main.c
	audioid.c
	minfft.c
	miniaudio.c
)

target_include_directories(SoundId PRIVATE
  .
)
