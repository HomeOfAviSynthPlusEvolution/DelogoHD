cmake_minimum_required(VERSION 3.24)

project(DelogoHD CXX)

option(DELOGOHD_BUILD_TEST_TOOLS "Build DelogoHD baseline test tools" ON)
option(DELOGOHD_COPY_RELEASE "Copy the built plugin to the legacy Release_<version> directory" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Git)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --first-parent --tags --always
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_REPO_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
else()
  set(GIT_REPO_VERSION "unknown")
endif()
string(REGEX REPLACE "^(r[0-9]+).*$" "\\1" VERSION "${GIT_REPO_VERSION}")

set(DELOGOHD_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
  "${PROJECT_SOURCE_DIR}/version.hpp.in"
  "${DELOGOHD_GENERATED_DIR}/version.hpp"
)
if(WIN32)
  configure_file(
    "${PROJECT_SOURCE_DIR}/version.rc.in"
    "${DELOGOHD_GENERATED_DIR}/version.rc"
  )
endif()

set(DELOGOHD_SOURCES
  avs_interface.cpp
  vs_interface.cpp
  delogo_engine.add.cpp
  delogo_engine.add_sse4.cpp
  delogo_engine.erase.cpp
  delogo_engine.erase_sse4.cpp
  delogo_engine.preprocess.cpp
  delogo_engine.hpp
  delogohd_filter.hpp
  wrapper/avs_filter.hpp
  wrapper/vs_filter.hpp
)
if(WIN32)
  list(APPEND DELOGOHD_SOURCES "${DELOGOHD_GENERATED_DIR}/version.rc")
endif()

add_library(DelogoHD SHARED ${DELOGOHD_SOURCES})
set_property(TARGET DelogoHD PROPERTY CXX_STANDARD 23)

target_compile_definitions(DelogoHD
  PRIVATE
    _CRT_NONSTDC_NO_DEPRECATE
)

target_include_directories(DelogoHD
  PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}"
    "${DELOGOHD_GENERATED_DIR}"
)

target_compile_options(DelogoHD
  PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-msse4.1>
    $<$<CXX_COMPILER_ID:Intel>:/arch:SSE4.1>
)
if(MSVC)
  target_compile_options(DelogoHD PRIVATE /arch:SSE2)
endif()

if(NOT WIN32)
  target_link_libraries(DelogoHD PRIVATE dl)
endif()

if(DELOGOHD_COPY_RELEASE AND DEFINED _DIR)
  add_custom_command(
    TARGET DelogoHD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      $<TARGET_FILE:DelogoHD>
      "../Release_${VERSION}/${_DIR}/$<TARGET_FILE_NAME:DelogoHD>"
  )
endif()

if(DELOGOHD_BUILD_TEST_TOOLS)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  add_executable(delogohd_avs_frame_dump
    tests/tools/avs_frame_dump.cpp
  )
  set_property(TARGET delogohd_avs_frame_dump PROPERTY CXX_STANDARD 17)
  target_include_directories(delogohd_avs_frame_dump PRIVATE include)
  if(NOT WIN32)
    target_link_libraries(delogohd_avs_frame_dump PRIVATE dl)
  endif()

  enable_testing()
  add_test(
    NAME baseline_runner_unit
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -v
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  )
endif()
