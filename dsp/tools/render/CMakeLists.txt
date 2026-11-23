# The render tool: WAV in, parameters in, WAV out. The WAV and command-line
# code lives in a small library so the tests can drive it too.
add_library(gigrig_render_lib STATIC wav.cpp render.cpp)
target_include_directories(gigrig_render_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gigrig_render_lib PUBLIC GigRig::dsp PRIVATE gigrig_options)

if(GIGRIG_BUILD_TOOLS)
  add_executable(gigrig_render main.cpp)
  target_link_libraries(gigrig_render PRIVATE gigrig_render_lib gigrig_options)
endif()
