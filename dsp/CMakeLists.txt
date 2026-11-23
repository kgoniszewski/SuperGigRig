# GigRigDSP - the portable real-time DSP core of SuperGigRig.
# Builds on Linux, macOS and iPadOS with no third-party dependencies.
cmake_minimum_required(VERSION 3.24)
project(GigRigDSP VERSION 0.1.0 LANGUAGES CXX)

option(GIGRIG_BUILD_TESTS "Build the GigRigDSP tests" ON)
option(GIGRIG_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(GIGRIG_BUILD_TOOLS "Build the command-line tools (gigrig_render)" ON)
option(GIGRIG_SANITIZE "Build with address and undefined-behaviour sanitisers" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Shared warning and sanitiser flags, applied to every target in this project.
add_library(gigrig_options INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(gigrig_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wdouble-promotion -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Woverloaded-virtual -Wnull-dereference)
  if(GIGRIG_WARNINGS_AS_ERRORS)
    target_compile_options(gigrig_options INTERFACE -Werror)
  endif()
  if(GIGRIG_SANITIZE)
    target_compile_options(gigrig_options INTERFACE
      -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(gigrig_options INTERFACE -fsanitize=address,undefined)
  endif()
endif()

# The core library. New DSP sources are added to this list.
add_library(gigrig_dsp STATIC
  src/amps/combo40.cpp
  src/amps/triode.cpp
  src/blocks/biquad.cpp
  src/blocks/delay_line.cpp
  src/blocks/fft.cpp
  src/blocks/oversampler.cpp
  src/cab/cabinet.cpp
  src/cab/convolver.cpp
  src/cab/impulse_response.cpp
  src/cab/room.cpp
  src/effects/boost.cpp
  src/effects/bucket_delay.cpp
  src/effects/env_filter.cpp
  src/effects/eq.cpp
  src/effects/gold_od.cpp
  src/effects/pre_fx.cpp
  src/effects/twin_od.cpp
  src/engine.cpp
  src/gigrig.cpp
  src/parameters.cpp)
add_library(GigRig::dsp ALIAS gigrig_dsp)
target_include_directories(gigrig_dsp
  PUBLIC include
  PRIVATE src)
target_link_libraries(gigrig_dsp PRIVATE gigrig_options)

# Internal headers (src/) are visible to the tools and tests so they can test
# the building blocks directly; the app only uses include/gigrig.
add_library(gigrig_dsp_internal INTERFACE)
target_include_directories(gigrig_dsp_internal INTERFACE src)
target_link_libraries(gigrig_dsp_internal INTERFACE gigrig_dsp)

if(GIGRIG_BUILD_TOOLS OR GIGRIG_BUILD_TESTS)
  add_subdirectory(tools/render)
  add_subdirectory(tools/irgen)
endif()

if(GIGRIG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
