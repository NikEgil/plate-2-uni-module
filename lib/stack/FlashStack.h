#ifndef FLASHSTACK_H
#define FLASHSTACK_H

#include <stdint.h>
#include <stddef.h>

class FlashStack {
public:
    static const size_t DATA_LENGTH = 198;   // длина hex-строки в байтах
    static const size_t MAX_RECORDS = 12000;  // максимальное количество записей

    FlashStack();

    // Инициализация буфера. Вызывать ОДИН раз при старте (после пробуждения из сна или сброса)
    bool begin();

    // Записать массив из DATA_LENGTH байт.
    // При заполнении до MAX_RECORDS самая старая запись автоматически удаляется.
    // Возвращает true при успешной записи.
    bool write(const uint8_t *data);

    // Прочитать и удалить самую старую запись.
    // buffer должен вмещать DATA_LENGTH байт.
    // Возвращает true, если запись прочитана, false если буфер пуст.
    bool read(uint8_t *buffer);

    // Узнать текущее число записей в буфере.
    size_t count();

    // Полная очистка буфера. Все записи удаляются.
    bool clear();

private:
    // Приватные методы будут в .cpp
};

#endif