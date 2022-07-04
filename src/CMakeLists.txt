cmake_minimum_required(VERSION 3.10)

add_executable(audioid
	main.c
	audioid.c
	minfft.c
	miniaudio.c
)

target_include_directories(audioid PRIVATE
  .
)

if(NOT WIN32)
	target_link_libraries(audioid
		m
		pthread
		dl
	)
endif()
