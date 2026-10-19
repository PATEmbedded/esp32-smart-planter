# Changelog

## 1.0.0 (2026-10-19)

First release.

- ESP-IDF firmware for a self-watering pot: calibrated capacitive soil sensor, watering in pulses with soak times, watering windows in local time and critical watering at any time
- Safety: empty-tank stop, a daily pump-time budget, a fault after pulses without effect, pulses timed by the pump output itself and a task watchdog
- Button for a manual pulse and for clearing faults; status LED
- Built on esp32-gpio-devices, esp32-time-sync and embedded-cpp-toolkit v1.0.0, as git submodules
- Host tests of the watering logic, including a simulated pot over several days; CI builds for ESP-IDF 5.5 and 6.0 (ESP32, ESP32-S3, ESP32-C3)
