#pragma once
#include <Arduino.h>

enum CanId: uint32_t {
    CAN_ID_STATUS_DATA = 0x001,
    CAN_ID_MAIN_LINE_DATA = 0x002,
    CAN_ID_BATTERY1_DATA = 0x003,
    CAN_ID_BATTERY2_DATA = 0x004,
    // Add more CAN IDs as needed
};
#pragma pack(push, 1)
struct StatusData {
    uint8_t status_main_line; // Status code for main line
    uint8_t status_battery1; // Status code for battery 1
    uint8_t status_battery2; // Status code for battery 2
    uint8_t status_apu; // Status code for APU
};
struct MainLineData {
    uint16_t voltage; // Voltage in volts
    uint16_t current; // Current in amperes
};
struct Battery1Data {
    uint16_t voltage; // Voltage in volts
};
struct Battery2Data {
    uint16_t voltage; // Voltage in volts
};


#pragma pack(pop)