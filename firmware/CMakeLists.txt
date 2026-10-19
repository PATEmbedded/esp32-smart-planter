cmake_minimum_required(VERSION 3.16)

# esp32-gpio-devices, esp32-time-sync and embedded-cpp-toolkit are git submodules in components/,
# pinned to their v1.0.0 releases. These switches turn their warnings into errors as well.
set(GPIODEV_WERROR ON)
set(TIME_SYNC_WERROR ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_planter)
