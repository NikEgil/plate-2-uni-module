// =====================================================================
//  rs.cpp – Модуль работы с RS-485 (Modbus RTU) для ESP32
// =====================================================================
//  Использует программный UART (SoftwareSerial) для работы с двумя
//  каналами RS-485 (RS1 и RS2). Управляет пином RE/DE для переключения
//  режимов приёма/передачи. Предоставляет функции инициализации,
//  выбора канала, отправки данных и приёма с таймаутом.
//  Весь код компилируется только при BOARD_TYPE == 0.
// =====================================================================

#include "rs.h"
#include <SoftwareSerial.h>

#if BOARD_TYPE == 0

using namespace RsModbus;

namespace {
SoftwareSerial rsSerial;           ///< Объект программного UART для RS-485
RsChannel activeChannel = RS_NONE; ///< Текущий активный канал (RS1/RS2/Нет)
uint8_t redePinNum = 0;            ///< Номер пина управления RE/DE (для RS-485)
} // namespace

namespace RsModbus {

// =====================================================================
// init() – Инициализация пина управления RS-485
// =====================================================================
/**
 * @brief Инициализирует пин RE/DE для RS-485 (управление приём/передача).
 * @param rede номер пина, подключенного к RE и DE (обычно объединены).
 * @details Устанавливает пин как OUTPUT и переводит в режим приёма (LOW).
 *          Функция должна вызываться до любых операций с RS-485.
 */
void init(uint8_t rede) {
    redePinNum = rede;
    pinMode(redePinNum, OUTPUT);
    digitalWrite(redePinNum, LOW); // По умолчанию режим приёма
}

// =====================================================================
// setChannel() – Выбор активного канала RS-485
// =====================================================================
/**
 * @brief Активирует или деактивирует указанный канал RS-485.
 * @param ch       канал: RS_CH1 или RS_CH2 (или RS_NONE для отключения)
 * @param activate true – включить канал, false – выключить.
 * @details При активации закрывает предыдущий канал, открывает новый
 *          с параметрами 9600 бод, 8N1, используя соответствующие пины
 *          (RS1RX/RS1TX или RS2RX/RS2TX). Устанавливает таймаут 50 мс.
 *          При выключении (activate=false) закрывает текущий канал и
 *          сбрасывает activeChannel = RS_NONE.
 *          Если канал уже активен, ничего не делает.
 */
void setChannel(RsChannel ch, bool activate) {
    if (!activate) {
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

// =====================================================================
// sendData() – Отправка данных по RS-485
// =====================================================================
/**
 * @brief Отправляет массив байт через активный RS-485 канал.
 * @param data указатель на данные
 * @param len  количество байт для отправки
 * @details Перед отправкой переключает пин RE/DE в режим передачи (HIGH),
 *          ждёт 50 мс для стабилизации, записывает данные через SoftwareSerial,
 *          вызывает flush() для гарантии завершения, затем возвращает пин
 *          в режим приёма (LOW) и ждёт ещё 50 мс для защиты от эха.
 *          Если канал не активен (activeChannel == RS_NONE) – функция ничего не
 * делает.
 */
void sendData(const byte *data, size_t len) {
    digitalWrite(redePinNum, HIGH);
    delay(50); // стабилизация RE/DE
    rsSerial.write(data, len);
    rsSerial.flush(); // ждём завершения передачи
    digitalWrite(redePinNum, LOW);
    delay(50); // защита от эха на линии
}

// =====================================================================
// receiveData() – Приём данных с таймаутом и вычислением длины
// =====================================================================
/**
 * @brief Принимает Modbus RTU-пакет через активный RS-485 канал.
 * @param buffer              указатель на буфер для приёма
 * @param maxLen              максимальный размер буфера
 * @param silenceTimeout_ms   время ожидания тишины после пакета (для отсечения
 * шума)
 * @return size_t             количество реально принятых байт (0 если ошибка)
 * @details Алгоритм:
 *          1. Читает 3 байта заголовка (Адрес, Функция, Кол-во данных) с
 * таймаутом 1 сек.
 *          2. По значению третьего байта (dataLen) вычисляет общую длину
 * пакета: totalLen = 3 (заголовок) + dataLen (данные) + 2 (CRC).
 *          3. Дочитывает оставшиеся байты до totalLen с таймаутом 1 сек.
 *          4. Ожидает тишину на линии (silenceTimeout_ms) для фильтрации шумов.
 *          Возвращает count (реальное число прочитанных байт).
 *          При ошибках (таймаут, буфер мал) возвращает 0.
 *          Функция не проверяет CRC – это обязанность вызывающего кода.
 */
size_t receiveData(byte *buffer, size_t maxLen, uint32_t silenceTimeout_ms) {
    if (!buffer || maxLen < 3 || activeChannel == RS_NONE)
        return 0;

    size_t count = 0;
    uint32_t start = millis();

    // 1. Читаем заголовок (3 байта: Адрес, Функция, Кол-во данных)
    while (count < 3 && (millis() - start < 1000)) {
        if (rsSerial.available()) {
            buffer[count++] = rsSerial.read();
        }
    }
    if (count < 3)
        return 0; // Таймаут или обрыв

    // 2. Вычисляем точную длину по Modbus RTU: 3 заголовка + N данных + 2 CRC
    uint8_t dataLen = buffer[2];
    size_t totalLen = 3 + dataLen + 2;
    if (totalLen > maxLen)
        totalLen = maxLen; // Защита от переполнения

    // 3. Дочитываем остаток пакета
    start = millis();
    while (count < totalLen && (millis() - start < 1000)) {
        if (rsSerial.available()) {
            buffer[count++] = rsSerial.read();
        }
    }

    // 4. Ждём тишину, чтобы "отсечь" шум после пакета
    uint32_t silenceStart = millis();
    while (rsSerial.available() == 0 &&
           (millis() - silenceStart < silenceTimeout_ms)) {
        delay(1);
    }

    return count;
}

} // namespace RsModbus

#endif // BOARD_TYPE == 0