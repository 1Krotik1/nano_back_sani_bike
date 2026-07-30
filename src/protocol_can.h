#pragma once
#include <Arduino.h>

enum CanId: uint32_t {
    CAN_ID_STATUS_DATA    = 0x001,
    CAN_ID_MAIN_LINE_DATA = 0x002,
    CAN_ID_BATTERY1_DATA  = 0x003,
    CAN_ID_BATTERY2_DATA  = 0x004,
};

// --- Статусы INA226 ---
#define INA226_OK           0x00
#define INA226_ERR_I2C      0x01
#define INA226_ERR_OV       0x02  // перенапряжение шины
#define INA226_ERR_OC       0x03  // перегруз по току
#define INA226_ERR_TEMP     0x04  // перегрев >85°C

// --- Статусы АЦП батарей ---
#define BAT_ADC_OK          0x00
#define BAT_ADC_ERR_OPEN    0x01  // разрыв цепи (ADC = 0)
#define BAT_ADC_ERR_OVER    0x02  // перегруз (>4.8 В на входе)

// --- Статусы APU ---
#define APU_OK              0x00
#define APU_ERR             0x01  // ошибка


#pragma pack(push, 1)
struct StatusData {
    uint8_t status_main_line; // Статус INA226 (INA226_OK / INA226_ERR_*)
    uint8_t status_battery1;  // Статус АЦП Bat1 (BAT_ADC_OK / BAT_ADC_ERR_*)
    uint8_t status_battery2;  // Статус АЦП Bat2 (BAT_ADC_OK / BAT_ADC_ERR_*)
    uint8_t status_apu;       // Статус вспомогательной силовой установки (APU_OK / APU_ERR_*)
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