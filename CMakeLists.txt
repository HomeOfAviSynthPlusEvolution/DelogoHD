cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(DelogoHD CXX)

option(DELOGOHD_BUILD_TEST_TOOLS "Build DelogoHD baseline test tools" ON)
option(DELOGOHD_COPY_RELEASE "Copy the built plugin to the legacy Release_<version> directory" OFF)
option(DELOGOHD_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis during compilation" OFF)
# Pin past v3.7.5 so fallback headers allow Visual Studio ARM64 builds.
set(DELOGOHD_AVISYNTHPLUS_TAG "751ce2021cab1c8405d04825ac2f7e8ac93e8fa0" CACHE STRING "AviSynthPlus tag used when pkg-config cannot find AviSynth headers")
set(DELOGOHD_VAPOURSYNTH_TAG "R72" CACHE STRING "VapourSynth tag used when pkg-config cannot find VapourSynth headers")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DELOGOHD_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")
    set(DELOGOHD_CLANG_TIDY "${CLANG_TIDY_EXE};--extra-arg=-Wno-unknown-argument;--extra-arg=-Wno-unused-command-line-argument")
  else()
    message(WARNING "clang-tidy requested but not found in PATH")
  endif()
endif()

if(MSVC AND NOT CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

include(FetchContent)

FetchContent_Declare(
  highway
  GIT_REPOSITORY https://github.com/google/highway.git
  GIT_TAG 1.4.0
)
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(highway)

set(DS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DS_BUILD_ACCEPTANCE_PLUGIN OFF CACHE BOOL "" FORCE)
if(MSVC)
  set(DS_MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}" CACHE STRING "" FORCE)
endif()
FetchContent_Declare(
  dualsynth2
  GIT_REPOSITORY https://github.com/HomeOfAviSynthPlusEvolution/dualsynth2.git
  GIT_TAG 0.1
)
FetchContent_MakeAvailable(dualsynth2)

function(delogohd_fetch_avisynth_headers out_var)
  FetchContent_Declare(
    avisynthplus_headers
    GIT_REPOSITORY https://github.com/AviSynth/AviSynthPlus.git
    GIT_TAG "${DELOGOHD_AVISYNTHPLUS_TAG}"
    # Full clone is required because the ARM64 header fix is pinned by commit.
    SOURCE_SUBDIR delogohd-no-cmake
  )
  FetchContent_MakeAvailable(avisynthplus_headers)
  FetchContent_GetProperties(avisynthplus_headers)
  set(${out_var} "${avisynthplus_headers_SOURCE_DIR}/avs_core/include" PARENT_SCOPE)
endfunction()

function(delogohd_fetch_vapoursynth_headers out_var)
  FetchContent_Declare(
    vapoursynth_headers
    GIT_REPOSITORY https://github.com/vapoursynth/vapoursynth.git
    GIT_TAG "${DELOGOHD_VAPOURSYNTH_TAG}"
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR delogohd-no-cmake
  )
  FetchContent_MakeAvailable(vapoursynth_headers)
  FetchContent_GetProperties(vapoursynth_headers)
  set(${out_var} "${vapoursynth_headers_SOURCE_DIR}/include" PARENT_SCOPE)
endfunction()

function(delogohd_add_vapoursynth_compat_headers target)
  set(compat_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vapoursynth-compat")
  file(MAKE_DIRECTORY "${compat_dir}/vapoursynth")
  foreach(header IN ITEMS
    VSConstants4.h
    VSHelper.h
    VSHelper4.h
    VSScript.h
    VSScript4.h
    VapourSynth.h
    VapourSynth4.h
  )
    file(WRITE
      "${compat_dir}/vapoursynth/${header}"
      "#pragma once\n#include <${header}>\n"
    )
  endforeach()
  target_include_directories(${target} INTERFACE "${compat_dir}")
endfunction()

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(AVISYNTH QUIET avisynth)
  pkg_check_modules(VAPOURSYNTH QUIET vapoursynth)
endif()

add_library(delogohd_avisynth_headers INTERFACE)
if(AVISYNTH_FOUND)
  target_include_directories(delogohd_avisynth_headers INTERFACE ${AVISYNTH_INCLUDE_DIRS})
  target_compile_options(delogohd_avisynth_headers INTERFACE ${AVISYNTH_CFLAGS_OTHER})
  message(STATUS "Using AviSynth headers from pkg-config")
else()
  delogohd_fetch_avisynth_headers(DELOGOHD_AVISYNTH_INCLUDE_DIRS)
  target_include_directories(delogohd_avisynth_headers INTERFACE ${DELOGOHD_AVISYNTH_INCLUDE_DIRS})
  message(STATUS "Using AviSynth headers from FetchContent (${DELOGOHD_AVISYNTHPLUS_TAG})")
endif()

add_library(delogohd_vapoursynth_headers INTERFACE)
if(VAPOURSYNTH_FOUND)
  target_include_directories(delogohd_vapoursynth_headers INTERFACE ${VAPOURSYNTH_INCLUDE_DIRS})
  target_compile_options(delogohd_vapoursynth_headers INTERFACE ${VAPOURSYNTH_CFLAGS_OTHER})
  message(STATUS "Using VapourSynth headers from pkg-config")
else()
  delogohd_fetch_vapoursynth_headers(DELOGOHD_VAPOURSYNTH_INCLUDE_DIRS)
  target_include_directories(delogohd_vapoursynth_headers INTERFACE ${DELOGOHD_VAPOURSYNTH_INCLUDE_DIRS})
  message(STATUS "Using VapourSynth headers from FetchContent (${DELOGOHD_VAPOURSYNTH_TAG})")
endif()
delogohd_add_vapoursynth_compat_headers(delogohd_vapoursynth_headers)

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
  src/core/canonical_math.hpp
  src/core/delogo_processor.cpp
  src/core/delogo_processor.hpp
  src/core/logo.h
  src/core/logo_file.cpp
  src/core/logo_file.hpp
  src/core/prepared_logo.cpp
  src/core/prepared_logo.hpp
  src/core/reciprocal_math.hpp
  src/core/row_kernel_c.cpp
  src/core/row_kernel_hwy.cpp
  src/core/row_kernel.hpp
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
    "${PROJECT_SOURCE_DIR}/src"
    "${DELOGOHD_GENERATED_DIR}"
)

target_link_libraries(DelogoHD PRIVATE hwy delogohd_avisynth_headers delogohd_vapoursynth_headers)

if(NOT WIN32)
  target_link_libraries(DelogoHD PRIVATE dl)
endif()
target_link_libraries(DelogoHD PRIVATE DualSynth::dualsynth)

if(DELOGOHD_CLANG_TIDY)
  set_target_properties(DelogoHD PROPERTIES CXX_CLANG_TIDY "${DELOGOHD_CLANG_TIDY}")
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
  target_link_libraries(delogohd_avs_frame_dump PRIVATE delogohd_avisynth_headers)
  if(NOT WIN32)
    target_link_libraries(delogohd_avs_frame_dump PRIVATE dl)
  endif()

  enable_testing()
  add_test(
    NAME baseline_runner_unit
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -v
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  )
  set_tests_properties(
    baseline_runner_unit
    PROPERTIES
      ENVIRONMENT
        "DELOGOHD_TEST_PLUGIN=$<TARGET_FILE:DelogoHD>;DELOGOHD_TEST_AVS_DUMP=$<TARGET_FILE:delogohd_avs_frame_dump>"
  )

  foreach(backend IN ITEMS purec highway)
    foreach(tier IN ITEMS smoke compat)
      add_test(
        NAME baseline_${backend}_${tier}
        COMMAND
          ${Python3_EXECUTABLE} -m tests.baseline.baseline_runner verify
          --plugin $<TARGET_FILE:DelogoHD>
          --avs-dump $<TARGET_FILE:delogohd_avs_frame_dump>
          --golden "${PROJECT_SOURCE_DIR}/tests/baseline/golden/${tier}.json"
          --tier ${tier}
          --backend ${backend}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      )
    endforeach()
  endforeach()
endif()
