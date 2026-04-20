#pragma once
#include <Arduino.h>
#include <defenitions.h>
#include <esp_adc_cal.h>
#include <Preferences.h>
#include <esp_sleep.h>

extern esp_adc_cal_characteristics_t adc_chars;
extern Preferences preferences;
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