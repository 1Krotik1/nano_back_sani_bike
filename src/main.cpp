#include <Arduino.h>
#include <SPI.h>
#include <mcp_can.h>
#include <Wire.h>
#include <GyverINA.h>
#include <protocol_can.h>

// --- MCP2515 ---
#define MCP2515_CS_PIN    10
#define MCP2515_INT_PIN   2

MCP_CAN CAN(MCP2515_CS_PIN);

// --- INA226 (главная линия) ---
INA226 ina226;

// --- АЦП батарей ---
#define BAT1_ADC_PIN    A1
#define BAT2_ADC_PIN    A2

// --- Калибровка АЦП (подставьте свои значения) ---
// Формула: real_voltage = adc_value * (reference_voltage / 1023) * divider
#define ADC_REFERENCE_VOLTAGE   5.0f
#define BAT_ADC_DIVIDER_RATIO   2.0f  // делитель напряжения, если есть

// --- Период отправки CAN ---
#define CAN_TX_INTERVAL_MS  100

uint32_t lastCanTxMs = 0;
bool canOk = false;
bool inaOk = false;

// --- Кешированные значения INA226 (читаем один раз за цикл) ---
float cachedInaVoltage = 0.0f;
float cachedInaCurrent = 0.0f;

// --- Определение статусов (использует кешированные/переданные значения) ---
uint8_t getIna226Status() {
    if (!inaOk) return INA226_ERR_I2C;
    if (cachedInaVoltage > 36.0f) return INA226_ERR_OV;
    if (cachedInaCurrent > 0.8f || cachedInaCurrent < -0.8f) return INA226_ERR_OC;
    return INA226_OK;
}

uint8_t getBatAdcStatus(uint16_t adc) {
    if (adc == 0) return BAT_ADC_ERR_OPEN;
    float v = adc * (ADC_REFERENCE_VOLTAGE / 1023.0f) * BAT_ADC_DIVIDER_RATIO;
    if (v > 5.2f) return BAT_ADC_ERR_OVER;
    return BAT_ADC_OK;
}



void setup() {
    Serial.begin(115200);
    while (!Serial) {}
    Serial.println(F("Nano Back Sani Bike - CAN TX start"));

    // --- I2C: INA226 (шунт 0.1 Ом, макс. ток 0.8 А, адрес 0x40) ---
    if (!ina226.begin()) {
        Serial.println(F("INA226 not found! Check wiring."));
    } else {
        ina226.setAveraging(INA226_AVG_X4);
        ina226.setSampleTime(INA226_VBUS, INA226_CONV_1100US);
        ina226.setSampleTime(INA226_VSHUNT, INA226_CONV_1100US);
        inaOk = true;
        Serial.println(F("INA226 initialized (0x40)"));
    }

    // --- MCP2515 CAN (кварц 8 МГц) ---
    pinMode(MCP2515_INT_PIN, INPUT);
    if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
        Serial.println(F("MCP2515 init failed!"));
    } else {
        CAN.setMode(MCP_NORMAL);
        // Фильтры: принимаем все стандартные ID
        CAN.init_Mask(0, MCP_STD, 0x000);
        CAN.init_Mask(1, MCP_STD, 0x000);
        for (int i = 0; i < 6; i++) CAN.init_Filt(i, MCP_STD, 0x000);
        canOk = true;
        Serial.println(F("MCP2515 initialized (500kbps, 8MHz)"));
    }
}

void loop() {
    uint32_t now = millis();
    if (now - lastCanTxMs < CAN_TX_INTERVAL_MS) return;
    lastCanTxMs = now;

    // --- Чтение INA226 (один раз за цикл, кешируем) ---
    if (inaOk) {
        cachedInaVoltage = ina226.getVoltage();
        cachedInaCurrent = ina226.getCurrent();
    } else {
        cachedInaVoltage = 0.0f;
        cachedInaCurrent = 0.0f;
    }

    // --- Чтение АЦП батарей (один раз за цикл) ---
    uint16_t adc1 = analogRead(BAT1_ADC_PIN);
    uint16_t adc2 = analogRead(BAT2_ADC_PIN);

    // --- Подготовка данных ---
    MainLineData mainLine;
    mainLine.voltage = (uint16_t)(cachedInaVoltage * 100.0f);
    mainLine.current = (uint16_t)(cachedInaCurrent * 100.0f);

    Battery1Data bat1;
    bat1.voltage = (uint16_t)(adc1 * (ADC_REFERENCE_VOLTAGE / 1023.0f) * BAT_ADC_DIVIDER_RATIO * 100.0f);

    Battery2Data bat2;
    bat2.voltage = (uint16_t)(adc2 * (ADC_REFERENCE_VOLTAGE / 1023.0f) * BAT_ADC_DIVIDER_RATIO * 100.0f);

    // --- Определение статусов (на уже считанных значениях) ---
    uint8_t stMain = getIna226Status();
    uint8_t stBat1 = getBatAdcStatus(adc1);
    uint8_t stBat2 = getBatAdcStatus(adc2);

    // --- Отправка CAN ---
    if (!canOk) return;
    byte txBuf[8];
    byte sndStat;

    // Status (все поля инициализированы)
    StatusData status;
    status.status_main_line = stMain;
    status.status_battery1  = stBat1;
    status.status_battery2  = stBat2;
    status.status_apu       = APU_ERR; // на Nano нет ВСУ → ошибка

    memset(txBuf, 0, sizeof(txBuf));
    memcpy(txBuf, &status, sizeof(StatusData));
    sndStat = CAN.sendMsgBuf(CAN_ID_STATUS_DATA, 0, sizeof(StatusData), txBuf);
    if (sndStat != CAN_OK) {
        Serial.print(F("CAN send STATUS failed: "));
        Serial.println(sndStat);
    }

    // Main line
    memset(txBuf, 0, sizeof(txBuf));
    memcpy(txBuf, &mainLine, sizeof(MainLineData));
    sndStat = CAN.sendMsgBuf(CAN_ID_MAIN_LINE_DATA, 0, sizeof(MainLineData), txBuf);
    if (sndStat != CAN_OK) {
        Serial.print(F("CAN send MAIN_LINE failed: "));
        Serial.println(sndStat);
    }

    // Battery 1
    memset(txBuf, 0, sizeof(txBuf));
    memcpy(txBuf, &bat1, sizeof(Battery1Data));
    sndStat = CAN.sendMsgBuf(CAN_ID_BATTERY1_DATA, 0, sizeof(Battery1Data), txBuf);
    if (sndStat != CAN_OK) {
        Serial.print(F("CAN send BAT1 failed: "));
        Serial.println(sndStat);
    }

    // Battery 2
    memset(txBuf, 0, sizeof(txBuf));
    memcpy(txBuf, &bat2, sizeof(Battery2Data));
    sndStat = CAN.sendMsgBuf(CAN_ID_BATTERY2_DATA, 0, sizeof(Battery2Data), txBuf);
    if (sndStat != CAN_OK) {
        Serial.print(F("CAN send BAT2 failed: "));
        Serial.println(sndStat);
    }

    // Диагностика
    Serial.print(F("CAN TX: main="));
    Serial.print(cachedInaVoltage);
    Serial.print(F("V "));
    Serial.print(cachedInaCurrent);
    Serial.print(F("A bat1="));
    Serial.print(bat1.voltage / 100.0f);
    Serial.print(F("V bat2="));
    Serial.print(bat2.voltage / 100.0f);
    Serial.print(F("V st="));
    Serial.print(stMain);
    Serial.print(F("/"));
    Serial.print(stBat1);
    Serial.print(F("/"));
    Serial.println(stBat2);
}

