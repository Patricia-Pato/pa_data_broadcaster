target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr_uart_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr_ble_pa_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr_timer_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr_os_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr_led_adapter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr_button_adapter.c
)
