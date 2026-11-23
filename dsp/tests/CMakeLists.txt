# Unit and measurement tests. Each test file registers TEST_CASEs with the
# minimal harness in test_framework.hpp; add new files to this list.
add_executable(gigrig_tests
  test_main.cpp
  test_passthrough.cpp)
target_link_libraries(gigrig_tests PRIVATE GigRig::dsp gigrig_options)
target_include_directories(gigrig_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME gigrig_tests COMMAND gigrig_tests)
