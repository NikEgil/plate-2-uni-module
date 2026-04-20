#include "rs.h"
#include <SoftwareSerial.h>
using namespace RsModbus;
namespace
{
    SoftwareSerial rsSerial;
    RsChannel activeChannel = RS_NONE;
    uint8_t redePinNum = 0;
}

namespace RsModbus
{
    void init(uint8_t rede)
    {
        redePinNum = rede;
        pinMode(redePinNum, OUTPUT);
        digitalWrite(redePinNum, LOW); // По умолчанию приём
    }

    void setChannel(RsChannel ch, bool activate)
    {
        if (!activate)
        {
            rsSerial.end();
            activeChannel = RS_NONE;
            return;
        }
        if (activeChannel == ch)
            return; // Уже на этом канале

        rsSerial.end(); // Сброс предыдущего состояния
        uint8_t rx = (ch == RS_CH1) ? RS1RX : RS2RX;
        uint8_t tx = (ch == RS_CH1) ? RS1TX : RS2TX;

        rsSerial.begin(9600, SWSERIAL_8N1, rx, tx);
        rsSerial.setTimeout(50);
        activeChannel = ch;
    }

    void sendData(const byte *data, size_t len)
    {
        digitalWrite(redePinNum, HIGH);
        delay(50); // 10 мс хватает на стабилизацию RE/DE. 200 мс блокирует SoftSerial
        rsSerial.write(data, len);
        rsSerial.flush(); // Ждёт завершения передачи
        digitalWrite(redePinNum, LOW);
        delay(50); // Защита от эха на линии
    }

    size_t receiveData(byte *buffer, size_t maxLen, uint32_t silenceTimeout_ms)
    {
        // for (int j = 0; j < maxLen; j++)
        // {
        //     Serial.print("0x");
        //     if (buffer[j] < 0x10)
        //         Serial.print("0"); // Добавляем ведущий ноль для однозначных HEX
        //     Serial.print(buffer[j], HEX);
        //     if (j != maxLen - 1)
        //         Serial.print(", ");
        // }
        Serial.println();
        if (!buffer || maxLen < 3 || activeChannel == RS_NONE)
            return 0;

        size_t count = 0;
        uint32_t start = millis();

        // 1. Читаем заголовок (3 байта: Адрес, Функция, Кол-во данных)
        while (count < 3 && (millis() - start < 1000))
        {
            if (rsSerial.available())
                buffer[count++] = rsSerial.read();
        }
        if (count < 3)
            return 0; // Таймаут или обрыв

        // 2. Вычисляем точную длину по Modbus RTU: 3 заголовка + N данных + 2 CRC
        uint8_t dataLen = buffer[2];
        size_t totalLen = 3 + dataLen + 2;
        if (totalLen > maxLen)
            totalLen = maxLen; // Защита от переполнения
        // Serial.println(totalLen);
        // 3. Дочитываем остаток пакета
        start = millis();
        while (count < totalLen && (millis() - start < 1000))
        {
            // Serial.println(count);
            if (rsSerial.available())
                buffer[count++] = rsSerial.read();
        }

        // 4. Ждем тишину, чтобы "отсечь" шум после пакета
        uint32_t silenceStart = millis();
        while (rsSerial.available() == 0 && (millis() - silenceStart < silenceTimeout_ms))
        {
            delay(1);
        }

        return count;
    }

}
