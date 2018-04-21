cmake_minimum_required(VERSION 3.1)

set (CMAKE_CXX_STANDARD 11)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(cmake/sourceGroupByFolder.cmake)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

project(ffplayer)

find_package(SDL2 REQUIRED) # SDL2_INCLUDE_DIR, SDL2_LIBRARIES
include_directories("${SDL2_INCLUDE_DIR}")

find_package(FFMPEG REQUIRED) # FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARIES
if(NOT FFMPEG_FOUND)
	message("couldn't find ffmpeg!")
endif()
include_directories("${FFMPEG_INCLUDE_DIRS}")

message("ffmpeg libs: ${FFMPEG_LIBRARIES}")

file(GLOB sources
	${CMAKE_SOURCE_DIR}/src/*.cpp
	${CMAKE_SOURCE_DIR}/src/*.h
	${CMAKE_SOURCE_DIR}/src/*.hpp
)

add_executable(${PROJECT_NAME} ${sources} )
SOURCE_GROUP_BY_FOLDER(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} ${FFMPEG_LIBRARIES})
