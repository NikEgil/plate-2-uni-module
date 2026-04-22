#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <defenitions.h>
#include <esp_adc_cal.h>
#include <esp_sleep.h>

extern esp_adc_cal_characteristics_t adc_chars;
extern Preferences preferences;
uint8_t readSwitchState();
uint8_t checkButton();
void waitForButtonRelease();

// Инициализация кнопок и настройки сна
void initPins();
int readBatteryVoltage();

void blink(int count, int delayy);

void printHEX(byte data[], int len);

void saveArrayToFlash(byte data[]);

bool loadArrayFromFlash(byte data[]);

void byteArrayToHexString(const byte *byteArray, int length, String str);

void enable_power(bool act);
void enable_sens(int port);
void enable_sim(bool act);

void addCRC(byte req[], int dataLength, byte response[]);

void outCRC(byte req[], int dataLength, byte outcrc[]);

void sleep(int time);
void printCurrentTime();              // DEC: YYYY-MM-DD HH:MM:SS
uint64_t getPackedTimeHex();          // ГГММДДЧЧММСС → uint64_t
void getPackedTimeBytes(byte buf[6]); // Для отправки в RS485

size_t preparePacket(uint8_t *buf, uint32_t id, uint8_t battery,byte date[6],
                     uint8_t signal1, uint8_t signal2);