#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <defenitions.h>

namespace SimModule {
// Инициализация (вызвать один раз в setup)
void begin(int rxPin = -1, int txPin = -1, uint32_t baud = 0);
// Включение/выключение модема (питание/перезагрузка)
void activate(bool act);

// Подключение к сети и GPRS
bool connect(const char *apn, const char *user = "", const char *pass = "");

// Отключение от сети
void disconnect();

// Статус
bool isConnection();
bool hasNetwork();

// Утилиты
int getSignalQuality();
String getLocalIP();
String getModemInfo();
// Прямой доступ к клиенту (для HTTP/MQTT)
void *getClient(); // возвращает TinyGsmClient*

struct NetTime {
    int year, month, day;
    int hour, minute, second;
    int timezone; // В четвертях часа (например, +12 = Москва)
    bool valid;
};
// Настройка времени
bool enableTimeSync();    // Включает AT+CLTS=1 (один раз при первом запуске)
NetTime getNetworkTime(); // Запрашивает время
time_t getTimestamp();
bool syncSystemClock();

}; // namespace SimModule