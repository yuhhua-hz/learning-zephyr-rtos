add_library(lightmodbus_lib STATIC
	lightmodbus.c
)

target_link_libraries(lightmodbus_lib PUBLIC zephyr_interface)

target_include_directories(lightmodbus_lib PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(lightmodbus_lib PRIVATE
    LIGHTMODBUS_MASTER
    LIGHTMODBUS_F03M
    LIGHTMODBUS_F04M
)