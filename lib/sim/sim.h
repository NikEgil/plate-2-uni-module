#pragma once
#include <Arduino.h>
#include <defenitions.h>
// #include <PubSubClient.h>
#if NET>0
namespace SimModule {
struct NetTime {
    int year, month, day;
    int hour, minute, second;
    int timezone; // В четвертях часа (например, +12 = Москва)
    bool valid;
};

// 🔹 3. begin() — исправляем инициализацию объектов
// void begin(int rxPin, int txPin, uint32_t baud) ;
void begin(int rxPin = -1, int txPin = -1, uint32_t baud = 0);
void activate(bool act);
bool connect(const char *apn, const char *user, const char *pass);
void disconnect();

bool isConnection();
bool hasNetwork();
int getSignalQuality();

String getLocalIP();
String getModemInfo();
void *getClient();
bool enableTimeSync();

NetTime getNetworkTime();
bool syncSystemClock();

time_t getTimestamp();

void buildTopic(char *outTopic, size_t size);
// 🔹 4. MQTT-функции — используем -> вместо .
bool mqttConnect();

bool mqttSendPacket(const uint8_t *payload, size_t length);
void mqttdisconnect();

} // namespace SimModule

#endif