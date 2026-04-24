#pragma once

constexpr int uS_TO_S_FACTOR = 1000000; // Конверсия микросекунд в секунды
constexpr int TIME_TO_SLEEP = 60 * 15;  // Время сна в секундах
constexpr int TIME_TO_SLEEP_long = 60 * 60 * 12;
constexpr int TIME_TO_SLEEP_error = 5;

constexpr int LED_PIN = 15;
constexpr int EG1 = 5;
constexpr int EG2 = 2;
constexpr int EG3 = 3;
constexpr int EG4 = 1;
constexpr int EP = 4;
// constexpr int E5V 6;
// constexpr int E3_3V 99
// constexpr int E12V 4

constexpr int RS1RX = 12;
constexpr int RS1TX = 13;
constexpr int RS2RX = 7;
constexpr int RS2TX = 6;
constexpr int REDE = 11;

constexpr int ESIM = 38;
constexpr int SIMRX = 17;
constexpr int SIMTX = 16;
constexpr int SIM_BAUD = 9600;
constexpr int ADC = 10;

constexpr int BUT1 = 8;
constexpr int BUT2 = 9;
constexpr int SW1_PIN =36;
constexpr int SW2_PIN= 35;

// constexpr int RSA 35
// constexpr int RSA 11

// const gpio_num_t button1 = GPIO_NUM_13;
// const gpio_num_t button2 = GPIO_NUM_12;

constexpr int MODEM_PWR_PIN = 40;
extern const char *apn;      // Access Point Name
extern const char *gprsUser; // GPRS username (if required)
extern const char *gprsPass; // GPRS password (if required)
extern const char *broker;
extern const int ID;
extern const char *IDchar;
extern const char *pass;

extern const int sensReg[];
extern const int sensTime[];
#define SWITCH_DEBOUNCE_MS 20
struct CalPoint {
    int raw;    // analogReadRaw()
    float vbat; // Напряжение батареи (мультиметр)
};
const CalPoint CAL_LOW = {.raw = 3988, .vbat = 3.601}; // Нижняя точка
const CalPoint CAL_HIGH = {.raw = 7004, .vbat = 4.176};