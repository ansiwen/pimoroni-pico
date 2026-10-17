if(NOT TARGET hub75)
  include(${CMAKE_CURRENT_LIST_DIR}/hub75.cmake)
endif()

add_library(hub75_spwm INTERFACE)

target_sources(hub75_spwm INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/hub75_spwm.cpp)

pico_generate_pio_header(hub75_spwm ${CMAKE_CURRENT_LIST_DIR}/hub75_spwm.pio)

target_include_directories(hub75_spwm INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(hub75_spwm INTERFACE hub75 pico_stdlib hardware_pio hardware_dma pico_graphics)
