#pragma once
#include <Arduino.h>
#include <LoRa_E220.h>
#include <defenitions.h>
// 🔹 Пины для ESP32-S2 (настраиваются при компиляции)

namespace LoRa {

// 🔹 Инициализация (вызывать в setup)
bool begin(uint32_t baud = LORA_BAUD);
bool end();
// 🔹 Информация о модуле
void printModuleInfo();
bool getModuleInfo(struct ModuleInformation *outInfo);
    int receivePacketNB(uint8_t* outBuf, size_t maxSize);

// 🔹 Опционально: отдельная проверка наличия данных
bool packetAvailable();// 🔹 Конфигурация
void configGet();                                 // Печать текущей конфигурации
bool configSet(uint8_t channel, uint8_t address); // Установка новой

// 🔹 Отправка/приём (базовые)
bool send(const uint8_t *data, size_t len);
// int receive(uint8_t* buffer, size_t maxSize, uint32_t timeout = 1000);
bool messageGetOK(uint32_t timeoutMs);
void messageGetOKrssi(int rssi, uint32_t timeoutMs);
// 🔹 Доступ к объекту (если нужно расширить функционал)
LoRa_E220 *getClient();

} // namespace LoRa