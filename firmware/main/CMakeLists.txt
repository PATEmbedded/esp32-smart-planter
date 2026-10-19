idf_component_register(
    SRCS "main.cpp" "moisture_input.cpp" "network.cpp"
    INCLUDE_DIRS "."
    REQUIRES planter_core esp32-gpio-devices esp32-time-sync esp_adc esp_wifi nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
