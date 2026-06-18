// =====================================================================
//  defenitions.h – Глобальные конфигурационные константы и определения пинов
// =====================================================================
//  Содержит константы, зависящие от:
//    - BOARD_REV   (версия платы: 1, 2, 3)
//    - BOARD_TYPE  (тип платы: 0, 1, 2)
//    - NET         (тип связи: 0 – LoRa, 1 – SIM800, 2 – LoRa+SIM)
//  Все значения задаются через препроцессор и используются в остальных модулях.
// =====================================================================

#pragma once

// ------------------------------------------------------------------
// Идентификатор устройства (задаётся в defenitions.cpp)
// ------------------------------------------------------------------
extern const int ID; ///< Уникальный ID устройства (используется в пакетах)
constexpr int uS_TO_S_FACTOR =
    1000000; ///< Коэффициент перевода секунд в микросекунды (для сна)
constexpr int TIME_TO_SLEEP = 60 * 15; ///< Базовое время сна (сек) – 15 минут
constexpr int TIME_TO_SLEEP_long =
    60 * 60 * 12;                      ///< Долгое время сна – 12 часов
constexpr int TIME_TO_SLEEP_error = 5; ///< Время сна при ошибке – 5 секунд

// ------------------------------------------------------------------
// Массивы конфигурации датчиков (определены в defenitions.cpp)
// ------------------------------------------------------------------
extern const int sensReg[]; ///< Массив: для каждого канала датчика – количество
                            ///< регистров для чтения (Modbus)
extern const int sensTime[]; ///< Массив: время опроса (сек) для каждого канала
                             ///< (0 – пропустить)
extern const int activeport[]; ///< Номера портов (1..4), которые используются в
                               ///< текущей конфигурации

#define SWITCH_DEBOUNCE_MS 20 ///< Время антидребезга для переключателей (мс)

// ------------------------------------------------------------------
// Структура калибровочной точки АЦП
// ------------------------------------------------------------------
struct CalPoint {
    int raw;    ///< Сырое значение АЦП (0..8191 при 13 бит)
    float vbat; ///< Соответствующее напряжение батареи (В)
};

extern const CalPoint CAL_LOW;  ///< Калибровочная точка для низкого напряжения
extern const CalPoint CAL_HIGH; ///< Калибровочная точка для высокого напряжения

// ------------------------------------------------------------------
// Пин встроенного светодиода (общий для всех плат)
// ------------------------------------------------------------------
constexpr int LED_PIN = 15;

// ------------------------------------------------------------------
// Назначение пинов в зависимости от BOARD_REV и BOARD_TYPE
// ------------------------------------------------------------------

#if BOARD_REV == 3 && BOARD_TYPE == 0
// Плата версии 3, тип 0 (основная, с портами EG1-EG4 и RS-485)
constexpr int EG1 = 5;      ///< Порт датчика 1 (управление питанием)
constexpr int EG2 = 2;      ///< Порт датчика 2
constexpr int EG3 = 3;      ///< Порт датчика 3
constexpr int EG4 = 1;      ///< Порт датчика 4
constexpr int EP = 4;       ///< Общее питание периферии
constexpr int ELORA = 33;   ///< Питание LoRa-модуля
constexpr int ESIM = 38;    ///< Питание SIM-модуля
constexpr int RS1RX = 12;   ///< RX первого RS-485 канала
constexpr int RS1TX = 13;   ///< TX первого RS-485 канала
constexpr int RS2RX = 7;    ///< RX второго RS-485 канала
constexpr int RS2TX = 6;    ///< TX второго RS-485 канала
constexpr int REDE = 11;    ///< Пин управления приём/передача RS-485 (RE/DE)
constexpr int ADC = 10;     ///< Пин измерения напряжения батареи
constexpr int SW1_PIN = 35; ///< Переключатель 1 (INPUT_PULLUP)
constexpr int SW2_PIN = 36; ///< Переключатель 2 (INPUT_PULLUP)

#elif BOARD_REV == 2 && BOARD_TYPE == 0
// Плата версии 2, тип 0 (с отдельными линиями 5В и 12В)
constexpr int EG1 = 5;
constexpr int EG2 = 2;
constexpr int EG3 = 3;
constexpr int EG4 = 1;
constexpr int EP = 0;   ///< Управление питанием (общий сигнал)
constexpr int E5V = 6;  ///< Включение 5В
constexpr int E12V = 4; ///< Включение 12В
constexpr int RS1RX = 40;
constexpr int RS1TX = 39;
constexpr int RS2RX = 38;
constexpr int RS2TX = 37;
constexpr int REDE = 11;
constexpr int ELORA = 8;
constexpr int SIMRX = 17;
constexpr int SIMTX = 16;
constexpr int SIM_BAUD = 9600; ///< Скорость UART для SIM800
constexpr int ADC = 2;
constexpr int BUT1 = 12; ///< Кнопка 1 (INPUT_PULLUP)
constexpr int BUT2 = 13; ///< Кнопка 2 (INPUT_PULLUP)
constexpr int SW1_PIN = 36;
constexpr int SW2_PIN = 35;

#elif BOARD_REV == 3 && BOARD_TYPE == 1
// Плата версии 3, тип 1 (упрощённая, только SIM и LoRa)
constexpr int EP = 2;      ///< Общее питание
constexpr int ELORA = 8;   ///< Питание LoRa
constexpr int ESIM = 3;    ///< Питание SIM
constexpr int ADC = 10;    ///< Измерение батареи
constexpr int BUT1 = 1;    ///< Кнопка 1 (INPUT_PULLUP)
constexpr int BUT2 = 9;    ///< Кнопка 2 (INPUT_PULLUP)
constexpr int SIM_PWR = 7; ///< Управление питанием SIM (активация)
constexpr int SW1_PIN = 6;
constexpr int SW2_PIN = 36;

#elif BOARD_REV == 3 && BOARD_TYPE == 2
// Плата версии 3, тип 2 (с кнопками и RS-485)
constexpr int EG1 = 5;
constexpr int EG2 = 2;
constexpr int EG3 = 3;
constexpr int EG4 = 1;
constexpr int EP = 4;
constexpr int ELORA = 33;
constexpr int ESIM = 38;
constexpr int RS1RX = 12;
constexpr int RS1TX = 13;
constexpr int RS2RX = 7;
constexpr int RS2TX = 6;
constexpr int REDE = 11;
constexpr int ADC = 10;
constexpr int BUT1 = 8; ///< Кнопка 1 (INPUT_PULLUP)
constexpr int BUT2 = 9; ///< Кнопка 2 (INPUT_PULLUP)
constexpr int SW1_PIN = 35;
constexpr int SW2_PIN = 36;

#elif BOARD_REV == 1 && BOARD_TYPE == 0
// Плата версии 1, тип 0 (старая, без LoRa и SIM)
constexpr int EG1 = 1;
constexpr int EG2 = 2;
constexpr int EG3 = 3;
constexpr int EG4 = 4;
constexpr int EP = 7; ///< Управление питанием
constexpr int RS1RX = 39;
constexpr int RS1TX = 40;
constexpr int RS2RX = 37;
constexpr int RS2TX = 38;
constexpr int ADC = 5;
constexpr int SW1_PIN = 35;
constexpr int SW2_PIN = 36;

#else
#error "Unknown BOARD_REV or BOARD_TYPE combination"
#endif

// ------------------------------------------------------------------
// Параметры связи в зависимости от NET
// ------------------------------------------------------------------
//
// Только SIM800
#if NET == 1 && BOARD_REV == 3
constexpr int SIMRX = 17;
constexpr int SIMTX = 16;
constexpr int SIM_BAUD = 9600;
constexpr int MODEM_PWR_PIN =
    40;                 ///< Пин управления питанием модема (если есть)
extern const char *apn; ///< APN для GPRS (задаётся в defenitions.cpp)
extern const char *gprsUser;
extern const char *gprsPass;
extern const char *broker; ///< MQTT брокер (если используется)
extern const char *IDchar; ///< ID в виде строки (для заголовков HTTP)
extern const char *pass;
//
// Только LoRa
#elif NET == 0 && BOARD_REV == 3
constexpr int LORA_UART_RX = 16;
constexpr int LORA_UART_TX = 17;
constexpr int LORA_AUX_PIN = 34; ///< E220 AUX
constexpr int LORA_M0_PIN = 18;  ///< E220 M0
constexpr int LORA_M1_PIN = 21;  ///< E220 M1
constexpr int LORA_BAUD = 9600;
constexpr int LORA_DEFAULT_CHANNEL = 17; ///< Канал по умолчанию
constexpr int LORA_DEFAULT_ADDRESS = 01; ///< Адрес по умолчанию
//
// LoRa + SIM800
#elif NET == 2 && BOARD_REV == 3
constexpr int SIMRX = 5;
constexpr int SIMTX = 4;
constexpr int SIM_BAUD = 2400;
constexpr int MODEM_PWR_PIN = 40;
extern const char *apn;
extern const char *gprsUser;
extern const char *gprsPass;
extern const char *IDchar;

constexpr int LORA_UART_RX = 16;
constexpr int LORA_UART_TX = 17;
constexpr int LORA_AUX_PIN = 34;
constexpr int LORA_M0_PIN = 18;
constexpr int LORA_M1_PIN = 21;
constexpr int LORA_BAUD = 9600;
constexpr int LORA_DEFAULT_CHANNEL = 17;
constexpr int LORA_DEFAULT_ADDRESS = 01;
//
// LoRa на старой плате

#elif NET == 0 && BOARD_REV == 1
constexpr int LORA_UART_RX = 16;
constexpr int LORA_UART_TX = 17;
constexpr int LORA_AUX_PIN = 8;
constexpr int LORA_M0_PIN = 18;
constexpr int LORA_M1_PIN = 21;
constexpr int LORA_BAUD = 9600;
constexpr int LORA_DEFAULT_CHANNEL = 17;
constexpr int LORA_DEFAULT_ADDRESS = 01;
#endif