cmake_minimum_required (VERSION 3.1)
project (DelogoHD)
file(GLOB API_FILES ${PROJECT_SOURCE_DIR}/*_interface.cpp)
file(GLOB CORE_FILES ${PROJECT_SOURCE_DIR}/delogo_engine.*.cpp)
add_library(DelogoHD SHARED ${API_FILES} ${CORE_FILES} version.rc)
set (VERSION "r7")
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
