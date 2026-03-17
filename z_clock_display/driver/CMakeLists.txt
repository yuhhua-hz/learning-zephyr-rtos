target_sources(app PRIVATE 
 src/z_ds1302.c
 src/z_display.c
 src/z_serial_console.c
)

target_include_directories(app PRIVATE
 ${CMAKE_CURRENT_SOURCE_DIR}/include
)