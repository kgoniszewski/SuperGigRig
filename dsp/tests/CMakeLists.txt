# Unit and measurement tests. Each test file registers TEST_CASEs with the
# minimal harness in test_framework.hpp; add new files to this list.
#
# alloc_guard.cpp replaces the global operator new for this binary so that
# tests can fail on any allocation inside process (CLAUDE.md, real-time rules).
add_executable(gigrig_tests
  test_main.cpp
  alloc_guard.cpp
  test_passthrough.cpp
  test_filters.cpp
  test_time_blocks.cpp
  test_nonlinear.cpp
  test_engine.cpp
  test_render.cpp
  test_pre_fx.cpp)
target_link_libraries(gigrig_tests PRIVATE GigRig::dsp gigrig_dsp_internal gigrig_render_lib gigrig_options)
target_include_directories(gigrig_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(gigrig_tests PRIVATE Threads::Threads)

add_test(NAME gigrig_tests COMMAND gigrig_tests)
