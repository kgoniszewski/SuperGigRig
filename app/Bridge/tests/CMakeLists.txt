# Linux tests for the platform-independent part of the app bridge (RealtimeIO.hpp and
# LoopbackProbe.hpp). The Objective-C++ and Swift code is checked by the Xcode build only.
#
#   cmake -S app/Bridge/tests -B build/bridge -G Ninja && cmake --build build/bridge
#   ctest --test-dir build/bridge --output-on-failure
cmake_minimum_required(VERSION 3.24)
project(GigRigBridgeTests LANGUAGES CXX)

set(GIGRIG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../dsp ${CMAKE_CURRENT_BINARY_DIR}/dsp)

add_executable(gigrig_bridge_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../dsp/tests/test_main.cpp
  test_realtime_io.cpp)
target_include_directories(gigrig_bridge_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../dsp/tests)
target_link_libraries(gigrig_bridge_tests PRIVATE GigRig::dsp gigrig_options)

enable_testing()
add_test(NAME gigrig_bridge_tests COMMAND gigrig_bridge_tests)
