idf_component_register(
    SRCS "src/moisture.cpp" "src/schedule.cpp" "src/watering.cpp"
    INCLUDE_DIRS "include"
    REQUIRES embedded-cpp-toolkit)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
