#pragma once
#include <Arduino.h>
#include <defenitions.h>

#if NET > 0

namespace SimModule {

struct NetTime {
    int year, month, day;
    int hour, minute, second;
    int timezone;
    bool valid;
};

// Инициализация (RX, TX, Baud берутся из defenitions.h, если переданы -1/0)
void begin(int rxPin = -1, int txPin = -1, uint32_t baud = 0);

// Корректное освобождение ресурсов (вызывать перед сном или отключением питания)
void end();

void activate(bool act);
bool connect(const char *apn, const char *user, const char *pass);
void disconnect();

int ccid(byte *ccid);
bool AT(const String &cmd, String &outResponse);

bool isConnection();
bool hasNetwork();
int getSignalQuality();

bool factoryReset();

String getLocalIP();
String getModemInfo();
void *getClient();

bool enableTimeSync();
NetTime getNetworkTime();

// Попытка синхронизации системных часов
// Возвращает: 1 - успех, 0 - ошибка, -1 - слишком рано (интервал < 3 сек)
int syncSystemClock();

time_t getTimestamp();
void buildTopic(char *outTopic, size_t size);

bool mqttConnect();
bool mqttSendPacket(const uint8_t *payload, size_t length);
void mqttDisconnect();

} // namespace SimModule
#endif