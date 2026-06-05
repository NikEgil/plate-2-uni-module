#ifndef FLASHSTACK_H
#define FLASHSTACK_H

#include <cstdint>
#include <cstddef>

class FlashStack {
public:
    static const size_t DATA_LENGTH = 198;   // подстройте под свой размер данных

    FlashStack();
    bool begin();                            // инициализация, один раз при старте
    bool write(const uint8_t *data);         // запись одного пакета
    bool read(uint8_t *buffer);              // чтение самого старого пакета (FIFO)
    size_t count();                          // сколько пакетов в буфере
    bool clear();                            // полная очистка (стирание всей памяти)
};

#endif