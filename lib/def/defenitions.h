#pragma once
#ifndef BOARD_REV
#define BOARD_REV 3
#endif
#ifndef NET
#define NET 1
#endif
extern const int ID;
constexpr int uS_TO_S_FACTOR = 1000000; // Конверсия микросекунд в секунды
constexpr int TIME_TO_SLEEP = 60 * 15;  // Время сна в секундах
constexpr int TIME_TO_SLEEP_long = 60 * 60 * 12;
constexpr int TIME_TO_SLEEP_error = 5;
extern const int sensReg[];
extern const int sensTime[];
extern const int activeport[];

#define SWITCH_DEBOUNCE_MS 20

struct CalPoint {
    int raw;    // analogReadRaw()
    float vbat; // Напряжение батареи (мультиметр)
};
const CalPoint CAL_LOW = {.raw = 3988, .vbat = 3.601}; // Нижняя точка
const CalPoint CAL_HIGH = {.raw = 7004, .vbat = 4.176};

constexpr int LED_PIN = 15;

#if BOARD_REV == 3 and BOARD_TYPE == 0
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
constexpr int BUT1 = 8;
constexpr int BUT2 = 9;
constexpr int SW1_PIN = 36;
constexpr int SW2_PIN = 35;
#elif BOARD_REV == 2 and BOARD_TYPE == 0
constexpr int EG1 = 5;
constexpr int EG2 = 2;
constexpr int EG3 = 3;
constexpr int EG4 = 1;
constexpr int EP = 0;
constexpr int E5V = 6;
constexpr int E12V = 4;
constexpr int RS1RX = 40;
constexpr int RS1TX = 39;
constexpr int RS2RX = 38;
constexpr int RS2TX = 37;
constexpr int REDE = 11;

constexpr int ESIM = 9;
constexpr int SIMRX = 17;
constexpr int SIMTX = 16;
constexpr int SIM_BAUD = 9600;
constexpr int ADC = 10;

constexpr int BUT1 = 12;
constexpr int BUT2 = 13;
constexpr int SW1_PIN = 36;
constexpr int SW2_PIN = 35;
#elif BOARD_REV == 3 and BOARD_TYPE == 1
constexpr int EP = 2;
constexpr int ELORA = 8;
constexpr int ESIM = 3;
constexpr int ADC = 10;
constexpr int BUT1 = 7;
constexpr int BUT2 = 9;
constexpr int SW1_PIN = 36;
constexpr int SW2_PIN = 35;
#else
#error "Unknown BOARD_REV"
#endif

#if NET == 1

constexpr int SIMRX = 17;
constexpr int SIMTX = 16;
constexpr int SIM_BAUD = 9600;
constexpr int MODEM_PWR_PIN = 40;
extern const char *apn;      // Access Point Name
extern const char *gprsUser; // GPRS username (if required)
extern const char *gprsPass; // GPRS password (if required)
extern const char *broker;

extern const char *IDchar;
extern const char *pass;

#elif NET == 0
constexpr int LORA_UART_RX = 16;
constexpr int LORA_UART_TX = 17;
constexpr int LORA_AUX_PIN = 34; // E220 AUX
constexpr int LORA_M0_PIN = 18;  // E220 M0
constexpr int LORA_M1_PIN = 21;  // E220 M1
constexpr int LORA_BAUD = 9800;
constexpr int LORA_DEFAULT_CHANNEL = 17;
constexpr int LORA_DEFAULT_ADDRESS = 01;
#elif NET==2
constexpr int SIMRX = 5;
constexpr int SIMTX = 4;
constexpr int SIM_BAUD = 9600;
constexpr int MODEM_PWR_PIN = 40;
extern const char *apn;      // Access Point Name
extern const char *gprsUser; // GPRS username (if required)
extern const char *gprsPass; // GPRS password (if required)
extern const char *broker;

extern const char *IDchar;
extern const char *pass;

constexpr int LORA_UART_RX = 16;
constexpr int LORA_UART_TX = 17;
constexpr int LORA_AUX_PIN = 34; // E220 AUX
constexpr int LORA_M0_PIN = 18;  // E220 M0
constexpr int LORA_M1_PIN = 21;  // E220 M1
constexpr int LORA_BAUD = 9800;
constexpr int LORA_DEFAULT_CHANNEL = 17;
constexpr int LORA_DEFAULT_ADDRESS = 01;

#endif