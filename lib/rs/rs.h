#pragma once
#include <Arduino.h>
#include <defenitions.h>
#include <sys.h>
#if BOARD_TYPE==0
namespace RsModbus {
enum RsChannel { RS_NONE = 0, RS_CH1 = 1, RS_CH2 = 2 };

void init(uint8_t redePin);
void setChannel(RsChannel ch, bool activate);
void sendData(const byte *data, size_t len);

// silenceTimeout: пауза в мс, после которой считаем, что передача закончилась.
// Для 9600 бод ставь 10-15 мс.
size_t receiveData(byte *buffer, size_t maxLen,
                   uint32_t silenceTimeout_ms = 10);
} // namespace RsModbus

#endif