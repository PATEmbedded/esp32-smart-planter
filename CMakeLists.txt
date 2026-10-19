# Host build of the watering logic and its tests. The firmware is the ESP-IDF project in firmware/.
cmake_minimum_required(VERSION 3.20)
project(esp32_smart_planter VERSION 1.0.0 LANGUAGES CXX)

option(PLANTER_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# The toolkit submodule the firmware uses, at the same release.
add_subdirectory(firmware/components/embedded-cpp-toolkit)

if(PLANTER_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

set(core firmware/components/planter_core)
add_library(planter_core STATIC ${core}/src/moisture.cpp ${core}/src/schedule.cpp ${core}/src/watering.cpp)
target_include_directories(planter_core PUBLIC ${core}/include)
target_link_libraries(planter_core PUBLIC ect::toolkit)

set(PLANTER_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
target_compile_options(planter_core PRIVATE ${PLANTER_WARNINGS})

enable_testing()
add_executable(planter_tests test/moisture_test.cpp test/schedule_test.cpp test/watering_test.cpp
                             test/simulated_pot_test.cpp)
target_link_libraries(planter_tests PRIVATE planter_core GTest::gtest_main)
target_compile_options(planter_tests PRIVATE ${PLANTER_WARNINGS})

include(GoogleTest)
gtest_discover_tests(planter_tests)
