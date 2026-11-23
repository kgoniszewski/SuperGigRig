# gigrig_irgen: generates the factory cabinet IRs in irs/ from an original
# speaker-and-mic model. The model is a library so a test can check that the
# committed files are exactly what the model produces.
add_library(gigrig_irgen_lib STATIC cab_model.cpp)
target_include_directories(gigrig_irgen_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gigrig_irgen_lib PUBLIC gigrig_dsp_internal PRIVATE gigrig_options)

if(GIGRIG_BUILD_TOOLS)
  add_executable(gigrig_irgen main.cpp)
  target_link_libraries(gigrig_irgen PRIVATE gigrig_irgen_lib gigrig_render_lib gigrig_options)
endif()
