cmake_minimum_required (VERSION 3.1)
project (DelogoHD)
add_library(DelogoHD SHARED avs_interface.cpp vs_interface.cpp version.rc)
set (VERSION "r5")
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
