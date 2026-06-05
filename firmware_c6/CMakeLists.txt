# ESP32-C6 Wi-Fi + BLE co-processor firmware
# Talks to the host over UART0 (via the on-board CH340G → COM4) using the same
# newline-delimited JSON protocol as the ESP32-P4 firmware.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32c6_wifi_bt)
