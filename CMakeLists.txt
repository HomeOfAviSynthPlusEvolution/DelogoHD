cmake_minimum_required (VERSION 3.1)
project (DelogoHD)
file(GLOB API_FILES ${PROJECT_SOURCE_DIR}/*_interface.cpp)
file(GLOB CORE_FILES ${PROJECT_SOURCE_DIR}/delogo_engine.*.cpp)
add_library(DelogoHD SHARED ${API_FILES} ${CORE_FILES} version.rc)
find_package(Git)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set_target_properties(DelogoHD PROPERTIES COMPILE_FLAGS "-msse4.1")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set_target_properties(DelogoHD PROPERTIES COMPILE_FLAGS "-msse4.1")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  set_target_properties(DelogoHD PROPERTIES COMPILE_FLAGS "-msse4.1 -std=c++17")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
  set_target_properties(DelogoHD PROPERTIES COMPILE_FLAGS "/arch:SSE4.1")
else()
  set_target_properties(DelogoHD PROPERTIES COMPILE_FLAGS "/arch:SSE2")
endif()
execute_process(COMMAND ${GIT_EXECUTABLE} describe --first-parent --tags --always OUTPUT_VARIABLE GIT_REPO_VERSION)
string(REGEX REPLACE "(r[0-9]+).*" "\\1" VERSION ${GIT_REPO_VERSION})
configure_file (
  "${PROJECT_SOURCE_DIR}/version.hpp.in"
  "${PROJECT_SOURCE_DIR}/version.hpp"
)
configure_file (
  "${PROJECT_SOURCE_DIR}/version.rc.in"
  "${PROJECT_SOURCE_DIR}/version.rc"
)
include_directories(include)
add_custom_command(
  TARGET DelogoHD POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:DelogoHD> "../Release_${VERSION}/${_DIR}/$<TARGET_FILE_NAME:DelogoHD>"
)
