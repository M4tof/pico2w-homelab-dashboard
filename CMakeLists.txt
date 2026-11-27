# 1. Minimum CMake version required
cmake_minimum_required(VERSION 3.13)

# 2. Include core SDK import script (DO NOT MODIFY)
include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

# 3. SET PROJECT NAME HERE
# Changing 'simple_blink' to your project name will rename your output file
project(pico_dashboard C CXX ASM)

# Define versioning here
set(FW_VERSION "2.0.10")
add_compile_options(-DNO_SYS=0)
add_definitions(-DFIRMWARE_VERSION="${FW_VERSION}")

# 4. Initialize SDK
pico_sdk_init()

# 5. DEFINE EXECUTABLE & SOURCE FILES
# List all your .c files here. 
# If you add 'mqtt.c' or 'sensors.c', add them to this list.
add_executable(pico_dashboard 
    certs.S
    src/main.c
    lib/api_server/api_server.c
    lib/lcd/lcd.c
    lib/cJSON/cJSON.c
    lib/mqtt/mqtt_client.c
    lib/light/light_read.c
    lib/network_watchdog/network_watchdog.c
    lib/display/display_driver.c
    lib/statusLED/statusLED.c
    lib/temp_sensor/temp_sensor.c
    lib/misc/misc.c
)

# Automatically rename the output file to include your firmware version
set_target_properties(pico_dashboard PROPERTIES 
    OUTPUT_NAME "pico_dashboard_v${FW_VERSION}"
)

# 6. Enable USB Serial output
pico_enable_stdio_usb(pico_dashboard 1)
pico_enable_stdio_uart(pico_dashboard 0)

# 7. ADD HEADER DIRECTORIES
# Use this if your .h files are in subfolders

target_include_directories(pico_dashboard PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/lib/cJSON
    ${CMAKE_CURRENT_LIST_DIR}/lib/lcd
    ${CMAKE_CURRENT_LIST_DIR}/lib/mqtt
    ${CMAKE_CURRENT_LIST_DIR}/lib/display
    ${CMAKE_CURRENT_LIST_DIR}/lib/light
    ${CMAKE_CURRENT_LIST_DIR}/lib/statusLED
    ${CMAKE_CURRENT_LIST_DIR}/lib/misc
    ${CMAKE_CURRENT_LIST_DIR}/lib/network_watchdog
    ${CMAKE_CURRENT_LIST_DIR}/lib/temp_sensor
    ${CMAKE_CURRENT_LIST_DIR}/lib/api_server
    ${CMAKE_CURRENT_LIST_DIR}
    ${FREERTOS_KERNEL_PATH}/include
    ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ/non_secure
)

# 8. LINK LIBRARIES
# Add external libraries here. 
# Examples: FreeRTOS-Kernel, pico_async_context_freertos, etc.
target_link_libraries(pico_dashboard PRIVATE 
    pico_stdlib 
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_mqtt
    pico_mbedtls
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    hardware_i2c
    hardware_adc
    hardware_pwm
)

# 9. Generate .uf2 file (DO NOT MODIFY)
pico_add_extra_outputs(pico_dashboard)
