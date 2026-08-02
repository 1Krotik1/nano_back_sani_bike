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
// Формула: real_voltage = adc_value * (reference_voltage / 1023)
#define ADC_REFERENCE_VOLTAGE   5.0f
#define ADC_FILTER_SAMPLES      5     // количество выборок для медианного фильтра

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
    float v = adc * (ADC_REFERENCE_VOLTAGE / 1023.0f);
    if (v >= ADC_REFERENCE_VOLTAGE) return BAT_ADC_ERR_OVER;
    return BAT_ADC_OK;
}

// --- Определение статуса напряжения батареи (5S Li-ion, без делителя, до 5В) ---
uint8_t getBatVoltStatus(float voltage) {
    if (voltage < 2.5f)  return BAT_VOLT_CRIT;   // критически низкое
    if (voltage < 3.0f)  return BAT_VOLT_LOW;    // низкое
    return BAT_VOLT_OK;                          // норма
}

// --- Медианный фильтр для АЦП (5 выборок) ---
uint16_t readAdcFiltered(uint8_t pin) {
    uint16_t samples[ADC_FILTER_SAMPLES];
    for (uint8_t i = 0; i < ADC_FILTER_SAMPLES; i++) {
        samples[i] = analogRead(pin);
        delayMicroseconds(500);
    }
    // Сортировка вставками для малого массива
    for (uint8_t i = 1; i < ADC_FILTER_SAMPLES; i++) {
        uint16_t key = samples[i];
        int8_t j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }
    return samples[ADC_FILTER_SAMPLES / 2];
}



// --- Универсальная отправка CAN-кадра ---
template<typename T>
byte sendCanFrame(uint32_t id, const T& data) {
    byte txBuf[8] = {0};
    memcpy(txBuf, &data, sizeof(T));
    return CAN.sendMsgBuf(id, 0, sizeof(T), txBuf);
}

void setup() {
    Serial.begin(115200);
    // Ждём Serial максимум 3 секунды, чтобы не зависнуть без USB
    uint32_t serialTimeout = millis();
    while (!Serial && (millis() - serialTimeout < 3000)) {}
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

    // --- Чтение АЦП батарей с медианным фильтром ---
    uint16_t adc1 = readAdcFiltered(BAT1_ADC_PIN);
    uint16_t adc2 = readAdcFiltered(BAT2_ADC_PIN);

    // --- Подготовка данных ---
    MainLineData mainLine;
    mainLine.voltage = (uint16_t)(cachedInaVoltage * 100.0f);
    mainLine.current = (uint16_t)(cachedInaCurrent * 100.0f);

    Battery1Data bat1;
    bat1.voltage = (uint16_t)(adc1 * (ADC_REFERENCE_VOLTAGE / 1023.0f) * 100.0f);

    Battery2Data bat2;
    bat2.voltage = (uint16_t)(adc2 * (ADC_REFERENCE_VOLTAGE / 1023.0f) * 100.0f);

    // --- Определение статусов (на уже считанных значениях) ---
    uint8_t stMain = getIna226Status();
    uint8_t stBat1 = getBatAdcStatus(adc1);
    uint8_t stBat2 = getBatAdcStatus(adc2);

    // --- Статусы напряжения батарей ---
    float bat1V = bat1.voltage / 100.0f;
    float bat2V = bat2.voltage / 100.0f;
    uint8_t stVoltBat1 = getBatVoltStatus(bat1V);
    uint8_t stVoltBat2 = getBatVoltStatus(bat2V);

    // --- Отправка CAN ---
    if (!canOk) return;
    byte sndStat;

    // Status (все поля инициализированы)
    StatusData status;
    status.status_main_line = stMain;
    status.status_battery1  = stBat1;
    status.status_battery2  = stBat2;
    status.status_apu       = APU_ERR; // на Nano нет ВСУ → ошибка
    status.volt_status_bat1 = stVoltBat1;
    status.volt_status_bat2 = stVoltBat2;
    status.reserved[0]      = 0;
    status.reserved[1]      = 0;

    sndStat = sendCanFrame(CAN_ID_STATUS_DATA, status);
    if (sndStat != CAN_OK) {
        Serial.print(F("CAN send STATUS failed: "));
        Serial.println(sndStat);
    }

    // Main line
    sndStat = sendCanFrame(CAN_ID_MAIN_LINE_DATA, mainLine);
    if (sndStat != CAN_OK) {
        Serial.print(F("CAN send MAIN_LINE failed: "));
        Serial.println(sndStat);
    }

    // Battery 1
    sndStat = sendCanFrame(CAN_ID_BATTERY1_DATA, bat1);
    if (sndStat != CAN_OK) {
        Serial.print(F("CAN send BAT1 failed: "));
        Serial.println(sndStat);
    }

    // Battery 2
    sndStat = sendCanFrame(CAN_ID_BATTERY2_DATA, bat2);
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

