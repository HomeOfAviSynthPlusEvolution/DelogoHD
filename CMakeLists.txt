cmake_minimum_required(VERSION 3.24)

project(DelogoHD CXX)

option(DELOGOHD_BUILD_TEST_TOOLS "Build DelogoHD baseline test tools" ON)
option(DELOGOHD_COPY_RELEASE "Copy the built plugin to the legacy Release_<version> directory" OFF)
option(DELOGOHD_PURE_C "Build DelogoHD with scalar C row kernels instead of SSE4.1 row kernels" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(DS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DS_BUILD_ACCEPTANCE_PLUGIN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  dualsynth2
  GIT_REPOSITORY https://github.com/HomeOfAviSynthPlusEvolution/dualsynth2.git
  GIT_TAG master
)
FetchContent_MakeAvailable(dualsynth2)

if(NOT MSVC)
  find_package(PkgConfig REQUIRED)

  pkg_check_modules(AVISYNTH avisynth)
  if(AVISYNTH_FOUND)
    set(DELOGOHD_AVISYNTH_INCLUDE_DIRS ${AVISYNTH_INCLUDE_DIRS})
  else()
    set(DELOGOHD_AVISYNTH_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include/avisynth")
  endif()

  pkg_check_modules(VAPOURSYNTH vapoursynth)
  if(VAPOURSYNTH_FOUND)
    set(DELOGOHD_VAPOURSYNTH_INCLUDE_DIRS ${VAPOURSYNTH_INCLUDE_DIRS})
  else()
    set(DELOGOHD_VAPOURSYNTH_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include/vapoursynth")
  endif()
else()
  set(DELOGOHD_AVISYNTH_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include/avisynth")
  set(DELOGOHD_VAPOURSYNTH_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include/vapoursynth")
endif()

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
  "${PROJECT_SOURCE_DIR}/src/version.hpp.in"
  "${DELOGOHD_GENERATED_DIR}/version.hpp"
)
if(WIN32)
  configure_file(
    "${PROJECT_SOURCE_DIR}/src/version.rc.in"
    "${DELOGOHD_GENERATED_DIR}/version.rc"
  )
endif()

set(DELOGOHD_SOURCES
  src/plugin/plugin_entry.cpp
  src/plugin/delogohd_filter.cpp
  src/plugin/delogohd_filter.hpp
  src/core/aligned_buffer.hpp
  src/core/delogo_processor.cpp
  src/core/delogo_processor.hpp
  src/core/logo.h
  src/core/logo_file.cpp
  src/core/logo_file.hpp
  src/core/prepared_logo.cpp
  src/core/prepared_logo.hpp
  src/core/row_kernel_c.cpp
  src/core/row_kernel.hpp
)
if(NOT DELOGOHD_PURE_C)
  list(APPEND DELOGOHD_SOURCES
    src/core/row_kernel_sse4.cpp
  )
endif()
if(WIN32)
  list(APPEND DELOGOHD_SOURCES "${DELOGOHD_GENERATED_DIR}/version.rc")
endif()

add_library(DelogoHD SHARED ${DELOGOHD_SOURCES})
set_property(TARGET DelogoHD PROPERTY CXX_STANDARD 23)

target_compile_definitions(DelogoHD
  PRIVATE
    _CRT_NONSTDC_NO_DEPRECATE
)
if(DELOGOHD_PURE_C)
  target_compile_definitions(DelogoHD PRIVATE PURE_C)
endif()

target_include_directories(DelogoHD
  PRIVATE
    ${DELOGOHD_AVISYNTH_INCLUDE_DIRS}
    ${DELOGOHD_VAPOURSYNTH_INCLUDE_DIRS}
    "${PROJECT_SOURCE_DIR}/src"
    "${DELOGOHD_GENERATED_DIR}"
)

if(NOT DELOGOHD_PURE_C)
  target_compile_options(DelogoHD
    PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-msse4.1>
      $<$<CXX_COMPILER_ID:Intel>:/arch:SSE4.1>
  )
  if(MSVC)
    target_compile_options(DelogoHD PRIVATE /arch:SSE2)
  endif()
endif()

if(NOT WIN32)
  target_link_libraries(DelogoHD PRIVATE dl)
endif()
target_link_libraries(DelogoHD PRIVATE DualSynth::dualsynth)

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
  target_include_directories(delogohd_avs_frame_dump PRIVATE ${DELOGOHD_AVISYNTH_INCLUDE_DIRS})
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
