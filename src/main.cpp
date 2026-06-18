// =====================================================================
//  main.cpp – Основной файл прошивки для устройства мониторинга
// =====================================================================
//  Поддерживает несколько конфигураций в зависимости от макросов:
//    - BOARD_REV (1, 2, 3) и BOARD_TYPE (0, 1, 2)
//    - NET (0 – LoRa, 1 – SIM800, 2 – LoRa+SIM)
//  Реализует:
//    - опрос датчиков через RS-485 (Modbus RTU)
//    - сохранение данных в кольцевой буфер во Flash (FlashStack)
//    - отправку данных через LoRa или HTTP(S) через SIM800
//    - приём команд по LoRa (установка времени, проверка связи)
//    - управление питанием и режимами сна
//    - веб-интерфейс для отладки (BOARD_TYPE == 2)
// =====================================================================

#include "FlashStack.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_task_wdt.h>

#if BOARD_TYPE == 2
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#endif

#include <defenitions.h>
#include <sys.h>

#if NET == 0
#include <lora.h>
#elif NET == 1
#include <sim.h>
#elif NET == 2
#include <lora.h>
#include <sim.h>
#endif

#if BOARD_TYPE == 0
#include <rs.h>
#endif

// =====================================================================
// Глобальные переменные и объекты
// =====================================================================

static char message[300];        ///< Буфер для формирования текстовых сообщений (логи, ошибки)
FlashStack stack;                ///< Кольцевой буфер во Flash для хранения пакетов
static uint8_t g_packet[198];    ///< Буфер для формирования пакета данных (заголовок + полезная нагрузка)
static uint8_t a_packet[250];    ///< Дополнительный буфер для сборки HTTP-пакета (с добавлением служебных полей)
static uint8_t rxBuffer[250];    ///< Буфер приёма LoRa/RS-485
Preferences prefs;               ///< Для работы с NVS (не используется, но объявлено)
int Battery = 146;               ///< Напряжение батареи в децивольтах (будет обновлено)
byte tableSens[5] = {0};         ///< Таблица найденных датчиков: для каждого порта (0..4) – адрес Modbus (0 – нет)
byte ccid[10] = {};              ///< ICCID SIM-карты (10 байт в BCD)
int signalp = 0;                 ///< Уровень сигнала (в процентах для LoRa, 0..100 для SIM)

String Link = "";                ///< Путь HTTP-запроса (например, "/gateway/packets")
String URL = "";                 ///< Хост для HTTP (например, "test.rootsapp.ru")
uint16_t Port = 0;               ///< Порт для HTTP (обычно 80)

// =====================================================================
// Вспомогательные функции для работы с логами и конфигурацией
// =====================================================================

/**
 * @brief Сохраняет текстовое сообщение в стек (кодирует в буфер и пишет во Flash).
 * @param msg строка для сохранения.
 */
void saveMSG(const char *msg) {
    encode_to_buffer(msg, g_packet);
    if (stack.write(g_packet)) {
        Serial.print("message saved:    ");
        Serial.println(msg);
    }
}

/**
 * @brief Загружает настройки HTTP из NVS (Preferences).
 * @details Читает ключи "url", "port", "link" из пространства "sim-config".
 *          Если данных нет, устанавливает значения по умолчанию.
 */
void loadConfigFromNVS() {
    Preferences prefs;
    prefs.begin("sim-config", true); // read-only
    if (prefs.isKey("url")) {
        URL = prefs.getString("url", "mqtt.rootsapp.ru");
        Port = prefs.getString("port", "80").toInt();
        Link = prefs.getString("link", "/gateway/packets");
        Serial.println("[NVS] Загружены настройки:");
        Serial.printf("  URL: %s\n", URL.c_str());
        Serial.printf("  Port:   %d\n", Port);
        Serial.printf("  Link:   %s\n", Link.c_str());
    } else {
        Serial.println("[NVS] Нет сохранённых настроек");
        Link = "/gateway/packets";
        URL = "test.rootsapp.ru";
        Port = 80;
    }
    prefs.end();
}

/**
 * @brief Сохраняет настройки HTTP в NVS.
 * @param gURL   хост
 * @param gPort  порт
 * @param gLink  путь
 */
void saveConfigFromNVS(String gURL, int gPort, String gLink) {
    Preferences prefs;
    prefs.begin("sim-config", false);
    prefs.putString("url", gURL);
    prefs.putString("link", gLink);
    prefs.putString("port", String(gPort));
    prefs.end();
    loadConfigFromNVS();
}

/**
 * @brief Полностью очищает стек (FlashStack) – стирает все записи.
 */
void cleanUpStack() {
    Serial.println(F("🧹 Cleaning stack..."));
    if (stack.clear()) {
        Serial.println(F("✅ Stack cleared"));
    }
}

// =====================================================================
// Работа с датчиками (только для BOARD_TYPE == 0)
// =====================================================================

#if BOARD_TYPE == 0 && BOARD_REV == 3

/**
 * @brief Поиск мультисенсора (датчик с адресом 0x01) на текущем RS-485 канале.
 * @return true если датчик ответил корректно.
 * @details Отправляет Modbus-запрос на чтение одного регистра (0x01, 0x03, 0x0000, 0x0001).
 *          Если в ответе первый байт 0x01 – датчик найден.
 */
bool searche_multisens() {
    Serial.println("founding multisens");
    byte req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
    Serial.println("	request:");
    printHEX(req, sizeof(req));
    RsModbus::sendData(req, sizeof(req));

    delay(500);

    byte response[32] = {0};
    size_t lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
    Serial.println("	responses:");
    printHEX(response, lenresponse);

    if (response[0] == 0x01) {
        Serial.println("multisens is FOUND!!!");
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Поиск датчиков на указанном порту (1 или 4).
 * @param port номер порта (1 или 4).
 * @return true если найден хотя бы один датчик.
 * @details Включает питание порта, активирует соответствующий RS-485 канал,
 *          сначала ищет мультисенсор, затем универсальный датчик (запрос 0xFF, 0x03, 0x07D0...).
 *          Результат сохраняется в tableSens[port] и во Flash.
 */
bool searchSensors(int port) {
    blink(1, 1000);
    enable_power(true);
    Serial.printf("			Search sensors, port	%i\n", port);
    enable_sens(port);
    delay(1000);
    if (port == 1) {
        RsModbus::setChannel(RsModbus::RS_CH1, true);
        Serial.println("Channel 1 activated");
    } else {
        RsModbus::setChannel(RsModbus::RS_CH2, true);
        Serial.println("Channel 2 activated");
    }
    delay(1000);

    if (searche_multisens()) {
        tableSens[port] = 0x01;
        saveArrayToFlash(tableSens);
        blink(1, 1000);
        return true;
    }
    Serial.println("founding sens");

    Serial.println("	request:");
    byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};

    int lenreq = 8;
    printHEX(req, lenreq);
    RsModbus::sendData(req, sizeof(req));
    delay(500);

    byte response[32] = {0};
    size_t lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
    Serial.println("	responses:");
    printHEX(response, lenresponse);

    if (response[0] == 0x00) {
        Serial.println("	wait meteo:");
        for (int i = 0; i < 80; i++) {
            delay(250);
            lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
            printHEX(response, lenresponse);
            if (response[0] != 0) {
                break;
            }
        }
    }

    Serial.printf("%i		FOUND		!!!\n", (int)response[0]);
    if (response[0] == 0x24) {
        Serial.println("meteostation detected");
    } else if (response[0] != 0x00) {
        Serial.println("another sens detected");
    } else {
        Serial.println("		NOT SENS	!!!");
    }

    tableSens[port] = response[0];
    saveArrayToFlash(tableSens);
    enable_sens(0);
    snprintf(message, sizeof(message),
             "search sensor, port %d, address sens %d", port, (int)response[0]);
    saveMSG(message);
    if (response[0] != 0x00) {
        blink((int)response[0], 100);
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Опрос одного датчика (Modbus) с заданным адресом и количеством регистров.
 * @param sens      адрес датчика (0x01..0xFF)
 * @param lenreg    количество регистров для чтения
 * @param outBuf    буфер для ответа
 * @param outBufSize размер буфера
 * @return int количество принятых байт, -1 при ошибке.
 * @details Формирует запрос, отправляет, ждёт ответ с таймаутом 150 мс.
 */
int polling(int sens, int lenreg, uint8_t *outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 5)
        return -1;

    byte req[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
    req[0] = (byte)sens;
    req[5] = (byte)lenreg;

    byte request[8];
    addCRC(req, 6, request);
    RsModbus::sendData(request, 8);
    delay(250);
    int received = RsModbus::receiveData(outBuf, outBufSize, 150);
    if (received > 0) {
        printHEX(outBuf, received);
    }
    return received;
}

/**
 * @brief Специальный опрос метеодатчика (адрес 0x24), который отправляет данные асинхронно.
 * @param sens      адрес (0x24)
 * @param lenreg    количество регистров (обычно 25)
 * @param outBuf    буфер для ответа
 * @param outBufSize размер буфера
 * @return int количество принятых байт, -1 при ошибке.
 * @details Метеодатчик присылает пакет длиной 25 байт с CRC-8. Функция ожидает до 40 попыток,
 *          проверяет CRC и возвращает данные при успехе.
 */
int pollingMeteo(int sens, int lenreg, uint8_t *outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 5)
        return -1;
    Serial.println("Wait meteo data");
    int received = 0;
    for (int i = 0; i < 40; i++) {
        received = RsModbus::receiveData(outBuf, outBufSize, 750);
        Serial.printf("Tray %i,  RX[%d]: ", i, received);
        printHEX(outBuf, received);
        if (outBuf[0] == 0x24 && check_wh65lp_crc(outBuf, received)) {
            return received;
        } else {
            delay(250);
        }
    }
    received = -1;
    return received;
}

/**
 * @brief Структура для хранения результатов измерения.
 */
struct MeasureResult {
    uint8_t *data; ///< Указатель на динамически выделенный буфер с данными
    size_t length; ///< Длина данных в байтах
    bool valid;    ///< true если измерение успешно и память выделена
};

/**
 * @brief Выполняет измерение на указанном порту.
 * @param port номер порта (1 или 4).
 * @return MeasureResult структуру с данными или invalid.
 * @details Включает питание, активирует канал, в зависимости от типа датчика (tableSens[port])
 *          выполняет опрос (одиночный, мультисенсор или метео) и упаковывает результат.
 *          Результат: первый байт – номер порта, затем данные датчика.
 */
MeasureResult measure(int port) {
    MeasureResult res = {nullptr, 0, false};
    digitalWrite(LED_PIN, HIGH);
    enable_power(true);
    Serial.printf("Measure, port %i\n", port);
    enable_sens(port);
    delay(1000);
    if (port == 1) {
        RsModbus::setChannel(RsModbus::RS_CH1, true);
        Serial.println("Channel 1 activated");
    } else {
        RsModbus::setChannel(RsModbus::RS_CH2, true);
        Serial.println("Channel 2 activated");
    }
    int sens = (int)tableSens[port];
    int lenreg = sensReg[sens];
    byte tempBuf[64];
    int received = 0;

    delay(sensTime[sens] * 1000);
    int s = 0;
    switch (sens) {
    case 0:
        Serial.println("Sensor disabled");
        break;
    case 1:
        res.length = 50;
        res.data = (uint8_t *)malloc(res.length);
        memset(res.data, 0x00, res.length);

        for (int i = 0; i < 5; i++) {
            received = polling(i + 1, lenreg, tempBuf, sizeof(tempBuf));
            if (received == 9 && checkCRC(tempBuf, received)) {
                res.data[s] = (byte)port;
                memcpy(res.data + s + 1, tempBuf, 9);
                s += 10;
            } else {
                Serial.printf("⚠️ Addr %d failed\n", i + 1);
            }
        }
        Serial.println("all packed");
        printHEX(res.data, res.length);
        res.valid = true;
        break;
    case 36:
        res.length = 26;
        res.data = (uint8_t *)malloc(res.length);
        received = pollingMeteo(sens, 25, tempBuf, res.length);
        if (received == 25) {
            res.data[0] = (byte)port;
            memcpy(res.data + 1, tempBuf, received);
            Serial.println("полученные данные");
            printHEX(tempBuf, received);
            Serial.println("скопированные данные");
            printHEX(res.data, res.length);
            res.valid = true;
        } else {
            Serial.println("⚠️ Meteo sensor failed");
            res.valid = false;
        }
        break;
    default:
        res.length = 5 + lenreg * 2 + 1;
        res.data = (uint8_t *)malloc(res.length);
        received = polling(sens, lenreg, tempBuf, res.length);
        if (received == (int)res.length - 1 && checkCRC(tempBuf, received)) {
            res.data[0] = (byte)port;
            memcpy(res.data + 1, tempBuf, received);
            printHEX(res.data, received);
            res.valid = true;
        } else {
            Serial.println("⚠️ Single sensor failed");
            res.valid = false;
        }
        break;
    }
    enable_sens(0);
    Serial.println("✅ Measured");
    digitalWrite(LED_PIN, LOW);
    return res;
}

/**
 * @brief Основная функция сбора данных: опрашивает все активные порты, формирует пакет и сохраняет в стек.
 * @details Для каждого порта из activeport[] вызывает measure(), копирует данные в буфер g_packet,
 *          добавляет CRC и записывает в FlashStack.
 */
void dataPrepare() {
    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    printCurrentTime();
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    uint8_t *packet = g_packet;
    memset(packet, 0, sizeof(g_packet));
    size_t len = preparePacket(packet, 198, ID, Battery, dateBytes);

    MeasureResult mRes;

    for (int port = 0; port < 2; port++) {
        mRes = measure(activeport[port]);
        Serial.printf("Port %d: valid=%d, data=%p, len=%d\n", activeport[port],
                      mRes.valid, mRes.data, mRes.length);
        if (mRes.valid && mRes.data != nullptr && mRes.length > 0) {
            size_t remaining = sizeof(packet) - len;
            if (mRes.length > remaining) {
                Serial.printf(
                    "⚠️ Measured data too large (%d > %d). Truncating.\n",
                    mRes.length, remaining);
                mRes.length = remaining;
            }
            memcpy(packet + len, mRes.data, mRes.length);
            len += mRes.length;
        } else {
            Serial.println("⚠️ No valid measured data to append");
        }
        if (mRes.valid && mRes.data != nullptr) {
            free(mRes.data);
            mRes.data = nullptr;
        }
        yield();
    }
    if (len < 198) {
        memset(packet + len, 0x00, 198 - len);
    }
    Serial.printf("Total packed length: %d bytes\n", len + 2);
    packet[0] = (byte)(len + 2);
    addCRC(packet, len, packet);
    printHEX(packet, len + 2);
    yield();
    if (stack.write(packet)) {
        Serial.printf("   ✅ Pushed packet (count: %d)\n", stack.count());
    } else {
        Serial.printf("   ❌ Failed to push packet #%d (stack full?)\n",
                      stack.count());
    }
    Serial.printf("   STACK count: %d)\n", stack.count());

    yield();
    enable_sens(0);
}

#elif BOARD_TYPE == 0 && BOARD_REV == 1

// Аналогичные функции для старой платы rev1 (почти идентичны, но с некоторыми отличиями в управлении портами)
// Здесь приведены полностью для полноты кода.

bool searche_multisens() {
    Serial.println("founding multisens");
    byte req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
    Serial.println("	request:");
    printHEX(req, sizeof(req));
    RsModbus::sendData(req, sizeof(req));
    delay(500);
    byte response[32] = {0};
    size_t lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
    Serial.println("	responses:");
    printHEX(response, lenresponse);
    if (response[0] == 0x01) {
        Serial.println("multisens is FOUND!!!");
        return true;
    } else {
        return false;
    }
}

bool searchSensors(int port) {
    blink(1, 1000);
    enable_power(true);
    Serial.printf("			Search sensors, port	%i\n", port);
    enable_sens(1);
    enable_sens(3);
    delay(1000);
    if (port == 1) {
        RsModbus::setChannel(RsModbus::RS_CH1, true);
        Serial.println("Channel 1 activated");
    } else {
        RsModbus::setChannel(RsModbus::RS_CH2, true);
        Serial.println("Channel 2 activated");
    }
    delay(1000);

    if (searche_multisens()) {
        tableSens[port] = 0x01;
        saveArrayToFlash(tableSens);
        blink(1, 1000);
        return true;
    }
    Serial.println("founding sens");
    Serial.println("	request:");
    byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};
    printHEX(req, 8);
    RsModbus::sendData(req, sizeof(req));
    delay(500);
    byte response[32] = {0};
    size_t lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
    Serial.println("	responses:");
    printHEX(response, lenresponse);
    if (response[0] == 0x00) {
        Serial.println("	wait meteo:");
        for (int i = 0; i < 80; i++) {
            delay(250);
            lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
            printHEX(response, lenresponse);
            if (response[0] != 0) {
                break;
            }
        }
    }
    Serial.printf("%i		FOUND		!!!\n", (int)response[0]);
    if (response[0] == 0x24) {
        Serial.println("meteostation detected");
    } else if (response[0] != 0x00) {
        Serial.println("another sens detected");
    } else {
        Serial.println("		NOT SENS	!!!");
    }
    tableSens[port] = response[0];
    saveArrayToFlash(tableSens);
    snprintf(message, sizeof(message),
             "search sensor, port %d, address sens %d", port, (int)response[0]);
    saveMSG(message);
    enable_sens(0);
    if (response[0] != 0x00) {
        blink((int)response[0], 100);
        return true;
    } else {
        return false;
    }
}

int polling(int sens, int lenreg, uint8_t *outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 5)
        return -1;
    byte req[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
    req[0] = (byte)sens;
    req[5] = (byte)lenreg;
    byte request[8];
    addCRC(req, 6, request);
    RsModbus::sendData(request, 8);
    delay(250);
    int received = RsModbus::receiveData(outBuf, outBufSize, 150);
    if (received > 0) {
        printHEX(outBuf, received);
    }
    return received;
}

int pollingMeteo(int sens, int lenreg, uint8_t *outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 5)
        return -1;
    Serial.println("Wait meteo data");
    int received = 0;
    for (int i = 0; i < 40; i++) {
        received = RsModbus::receiveData(outBuf, outBufSize, 750);
        Serial.printf("Tray %i,  RX[%d]: ", i, received);
        printHEX(outBuf, received);
        if (outBuf[0] == 0x24 && check_wh65lp_crc(outBuf, received)) {
            return received;
        } else {
            delay(250);
        }
    }
    received = -1;
    return received;
}

struct MeasureResult {
    uint8_t *data;
    size_t length;
    bool valid;
};

MeasureResult measure(int port) {
    MeasureResult res = {nullptr, 0, false};
    digitalWrite(LED_PIN, HIGH);
    enable_power(true);
    Serial.printf("Measure, port %i\n", port);
    enable_sens(1);
    enable_sens(3);
    delay(1000);
    if (port == 1) {
        RsModbus::setChannel(RsModbus::RS_CH1, true);
        Serial.println("Channel 1 activated");
    } else {
        RsModbus::setChannel(RsModbus::RS_CH2, true);
        Serial.println("Channel 2 activated");
    }
    int sens = (int)tableSens[port];
    int lenreg = sensReg[sens];
    byte tempBuf[64];
    int received = 0;

    delay(sensTime[sens] * 1000);
    int s = 0;
    switch (sens) {
    case 0:
        Serial.println("Sensor disabled");
        snprintf(message, sizeof(message), "ERROR port %d sensor not find",
                 port);
        saveMSG(message);
        break;
    case 1:
        res.length = 50;
        res.data = (uint8_t *)malloc(res.length);
        memset(res.data, 0x00, res.length);

        for (int i = 0; i < 5; i++) {
            received = polling(i + 1, lenreg, tempBuf, sizeof(tempBuf));
            if (received == 9 && checkCRC(tempBuf, received)) {
                res.data[s] = (byte)port;
                memcpy(res.data + s + 1, tempBuf, 9);
                s += 10;
            } else {
                Serial.printf("⚠️ Addr %d failed\n", i + 1);
                snprintf(message, sizeof(message),
                         "ERROR port %d measuring fail, address sens %d", port,
                         sens);
                saveMSG(message);
            }
        }
        Serial.println("all packed");
        printHEX(res.data, res.length);
        res.valid = true;
        break;
    case 36:
        res.length = 26;
        res.data = (uint8_t *)malloc(res.length);
        received = pollingMeteo(sens, 25, tempBuf, res.length);
        if (received == 25) {
            res.data[0] = (byte)port;
            memcpy(res.data + 1, tempBuf, received);
            Serial.println("полученные данные");
            printHEX(tempBuf, received);
            Serial.println("скопированные данные");
            printHEX(res.data, res.length);
            res.valid = true;
        } else {
            Serial.println("⚠️ Meteo sensor failed");
            res.valid = false;
            snprintf(message, sizeof(message),
                     "ERROR port %d measuring fail, address sens %d", port,
                     sens);
            saveMSG(message);
        }
        break;
    default:
        res.length = 5 + lenreg * 2 + 1;
        res.data = (uint8_t *)malloc(res.length);
        received = polling(sens, lenreg, tempBuf, res.length);
        if (received == (int)res.length - 1 && checkCRC(tempBuf, received)) {
            res.data[0] = (byte)port;
            memcpy(res.data + 1, tempBuf, received);
            printHEX(res.data, received);
            res.valid = true;
        } else {
            Serial.println("⚠️ Single sensor failed");
            snprintf(message, sizeof(message),
                     "ERROR port %d measuring fail, address sens %d", port,
                     sens);
            saveMSG(message);
            res.valid = false;
        }
        break;
    }
    enable_sens(0);
    Serial.println("✅ Measured");
    digitalWrite(LED_PIN, LOW);
    return res;
}

void dataPrepare() {
    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    printCurrentTime();
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    uint8_t *packet = g_packet;
    memset(packet, 0, sizeof(g_packet));
    size_t len = preparePacket(packet, 198, ID, Battery, dateBytes);

    MeasureResult mRes;

    for (int port = 0; port < 2; port++) {
        mRes = measure(activeport[port]);
        Serial.printf("Port %d: valid=%d, data=%p, len=%d\n", activeport[port],
                      mRes.valid, mRes.data, mRes.length);
        if (mRes.valid && mRes.data != nullptr && mRes.length > 0) {
            size_t remaining = sizeof(packet) - len;
            if (mRes.length > remaining) {
                Serial.printf(
                    "⚠️ Measured data too large (%d > %d). Truncating.\n",
                    mRes.length, remaining);
                mRes.length = remaining;
            }
            memcpy(packet + len, mRes.data, mRes.length);
            len += mRes.length;
        } else {
            Serial.println("⚠️ No valid measured data to append");
            saveMSG("ERROR  No valid measured data to append");
        }
        if (mRes.valid && mRes.data != nullptr) {
            free(mRes.data);
            mRes.data = nullptr;
        }
        yield();
    }
    if (len < 198) {
        memset(packet + len, 0x00, 198 - len);
    }
    Serial.printf("Total packed length: %d bytes\n", len + 2);
    packet[0] = (byte)(len + 2);
    addCRC(packet, len, packet);
    printHEX(packet, len + 2);
    yield();
    if (stack.write(packet)) {
        Serial.printf("   ✅ Pushed packet (count: %d)\n", stack.count());
    } else {
        Serial.printf("   ❌ Failed to push packet #%d (stack full?)\n",
                      stack.count());
    }
    Serial.printf("   STACK count: %d)\n", stack.count());

    yield();
    enable_sens(0);
}

#endif // BOARD_TYPE == 0

// =====================================================================
// Функции для работы с SIM800 (NET == 1 или 2)
// =====================================================================

#if NET == 1 || NET == 2

/**
 * @brief Получает сетевое время через SIM и синхронизирует системные часы ESP32.
 * @details Включает синхронизацию (AT+CLTS=1), ждёт 10 секунд, затем вызывает syncSystemClock().
 *          При успехе мигает светодиодом 2 раза.
 */
void getNetTime() {
    Serial.println("Enabling time sync...");
    if (SimModule::enableTimeSync()) {
    }
    delay(10000);
    for (int i = 0; i < 3; i++) {
        if (SimModule::syncSystemClock()) {
            Serial.println("✓ Time ready");
            blink(2, 1000);
            break;
        } else {
            Serial.println("fail");
            saveMSG("ERROR get net time");
            delay(3000);
        }
    }
    printCurrentTime();
}

/**
 * @brief Выполняет сброс SIM-модема и переинициализацию (используется для принудительного восстановления).
 */
void simres() {
    Serial.println("sim pwr low");
    delay(1000);
    Serial.println("sim pwr high");
    yield();
    SimModule::begin();
    SimModule::activate(true);
    SimModule::factoryReset();
    SimModule::disconnect();
    SimModule::activate(false);
    SimModule::end();
}

/**
 * @brief Обрабатывает одну SMS-команду для настройки параметров HTTP.
 * @param text текст SMS (ожидается формат: "ID url port link")
 * @return true если ID совпадает и настройки сохранены.
 */
bool processOneSMS(const String &text) {
    Serial.println("\n===== SMS =====");
    Serial.println(text);
    String parts[4];
    int partIndex = 0;
    int startPos = 0;
    for (int i = 0; i <= text.length() && partIndex < 4; i++) {
        if (i == text.length() || text[i] == ' ' || text[i] == '\n' ||
            text[i] == '\r') {
            if (i > startPos) {
                parts[partIndex] = text.substring(startPos, i);
                partIndex++;
            }
            startPos = i + 1;
        }
    }
    if (partIndex < 4) {
        Serial.println("[SMS] Неверный формат (нужно 4 поля)");
        return false;
    }
    String id = parts[0];
    String gURL = parts[1];
    uint16_t gPort = parts[2].toInt();
    String gLink = parts[3];
    if (id != String(IDchar)) {
        Serial.printf("[SMS] ID не совпадает: SMS=%s, Device=%s\n", id.c_str(),
                      IDchar);
        return false;
    } else {
        saveConfigFromNVS(gURL, gPort, gLink);
        return true;
    }
}

/**
 * @brief Читает все непрочитанные SMS и обрабатывает их.
 */
void readSMS() {
    String sms = "  ";
    Serial.println("read sms");
    while (sms != "") {
        sms = SimModule::getUnreadSMS();
        if (sms != "") {
            if (processOneSMS(sms)) {
                Serial.println("sms ok");
                saveMSG("sms get,config set");
            }
        }
    }
    Serial.println("ALL SMS READED");
}

/**
 * @brief Активирует SIM-модем, подключается к GPRS, получает время и читает SMS.
 * @param act true – включить, false – выключить.
 * @return true если удалось подключиться и выполнить все операции.
 */
bool sim_activate(bool act) {
    if (!act) {
        SimModule::disconnect();
        SimModule::activate(false);
        SimModule::end();
        Serial.println("\tsim disconnected");
        return true;
    }

    const int MAX_ROUNDS = 2;
    const unsigned long ROUND_DELAY = 3000;
    Serial.println("try conect0");

    for (int round = 1; round <= MAX_ROUNDS; round++) {
        Serial.printf("=== Round %d/%d ===\n", round, MAX_ROUNDS);
        yield();

        esp_task_wdt_reset();
        yield();
        SimModule::begin();
        delay(2000);
        esp_task_wdt_reset();
        yield();

        SimModule::activate(true);
        delay(5000);
        Serial.println("try conect");
        if (SimModule::connect(apn, gprsUser, gprsPass)) {
            Serial.println("\tsim connected");
            signalp = SimModule::getSignalQuality();
            SimModule::ccid(ccid);
            printHEX(ccid, 10);
            printCurrentTime();
            if (!isTime()) {
                getNetTime();
            }
            blink(1, 1000);
            readSMS();
            return true;
        }
        Serial.println("Round failed, powering off...");
        SimModule::disconnect();
        SimModule::activate(false);
        SimModule::end();
        delay(ROUND_DELAY);
    }
    Serial.println("All rounds failed");
    saveMSG("ERROR two attemp connection to net failed");
    return false;
}

/**
 * @brief Проверяет уровень сигнала SIM и мигает светодиодом в зависимости от качества.
 */
void SIM_check_signal() {
    Serial.println(">>> SIM CHECK SIGNAL");
    blink(1, 1500);
    if (sim_activate(true)) {
        delay(2000);
        if (signalp <= 20) {
            Serial.printf("signal %i, 1\n", signalp);
            blink(1, 750);
        } else if (signalp <= 40) {
            Serial.printf("signal %i, 2\n", signalp);
            blink(2, 750);
        } else if (signalp <= 60) {
            Serial.printf("signal %i, 3\n", signalp);
            blink(3, 750);
        } else if (signalp <= 80) {
            Serial.printf("signal %i, 4\n", signalp);
            blink(4, 750);
        } else {
            Serial.printf("signal %i, 5\n", signalp);
            blink(5, 750);
        }
    } else {
        blink(10, 250);
    }
    sim_activate(false);
}

/**
 * @brief Выполняет сброс SIM-модема к заводским настройкам (AT&F и перезагрузка).
 */
void SIM_reset() {
    enable_power(1);
    enable_sim(1);
    delay(5000);
    SimModule::begin();
    SimModule::activate(true);
    if (SimModule::factoryReset()) {
        Serial.println("Modem reset done. Reconnecting...");
    }
    enable_sim(0);
    enable_power(0);
}

/**
 * @brief Формирует HTTP-пакет, добавляя к данным заголовок с ID, батареей, сигналом и т.д.
 * @return длину сформированного пакета.
 */
int adding() {
    memset(a_packet, 0, 250);
    memcpy(a_packet, ccid, 10);

    a_packet[10] = (byte)(ID >> 16) & 0xFF;
    a_packet[11] = (byte)(ID >> 8) & 0xFF;
    a_packet[12] = (byte)(ID) & 0xFF;
    a_packet[13] = (byte)Battery;
    a_packet[14] = (byte)signalp;

    Serial.printf("my bat %i, my signal %i\n", Battery, signalp);
    int lgp = (int)g_packet[0];
    if (g_packet[lgp] == 0x00) {
        Serial.println("no lora data");
        a_packet[15] = 0x00;
    } else {
        Serial.println("its lora data");
        a_packet[15] = rssip(g_packet[lgp]);
    }
    int len = 16;
    Serial.println("a_packet:");
    memcpy(a_packet + len, g_packet, lgp);
    return len + lgp;
}

/**
 * @brief Отправляет все пакеты из стека по HTTP через SIM800.
 * @return true если все пакеты отправлены успешно, иначе false.
 * @details Для каждого пакета вызывает httpSendPacketSafe с автоматическими повторами.
 *          При трёх последовательных неудачах прерывает сессию.
 */
bool http_send() {
    yield();
    int pac_to_send = stack.count();
    Serial.printf("packets to sending - %i\n", pac_to_send);
    int consecutive_failures = 0;
    SimModule::httpBegin(URL.c_str(), Port);
    for (int i = 0; i < pac_to_send; i++) {
        yield();
        if (!stack.read(g_packet)) {
            Serial.printf("❌ Failed to pop packet #%d (stack empty?)\n", i);
            saveMSG("ERROR fail pop packet");
            return false;
        }
        Serial.printf("    Popped packet #%d\n", i);
        printHEX(g_packet, g_packet[0]);
        int len = adding();
        Serial.printf("   Prepared packet #%d, len %i\n", i, len);
        printHEX(a_packet, len);
        esp_task_wdt_reset();

        if (SimModule::httpSendPacketSafe(a_packet, len, IDchar, Link.c_str(),
                                          URL.c_str(), Port)) {
            Serial.printf("   ✅ packet #%d sent \n", i);
            blink(1, 500);
            consecutive_failures = 0;
            esp_task_wdt_reset();
            yield();
        } else {
            Serial.printf("   ❌ packet #%d NOT sent\n", i);
            stack.write(g_packet);
            consecutive_failures++;
            esp_task_wdt_reset();
            yield();
            if (consecutive_failures >= 3) {
                SimModule::httpEnd();
                return false;
            }
        }
        delay(50);
    }
    SimModule::httpEnd();
    yield();
    return true;
}
#endif // NET == 1 || NET == 2

// =====================================================================
// Функции для работы с LoRa (NET == 0 или 2)
// =====================================================================

#if NET == 0 || NET == 2

/**
 * @brief Активирует или деактивирует LoRa-модуль (включает питание и инициализирует/закрывает UART).
 * @param act true – включить, false – выключить.
 */
void lora_activate(bool act) {
    if (act) {
        enable_lora(1);
        delay(1000);
        Serial.printf("LoRa:: begin, %i\n", LoRa::begin());
        delay(1000);
    } else {
        Serial.printf("LoRa:: begin, %i\n", LoRa::end());
        enable_lora(0);
        delay(1000);
    }
}

/**
 * @brief Отправляет подтверждение (ACK) на полученный LoRa-пакет.
 * @details Формирует пакет с ID, маркером 0xFF и текущим временем, отправляет.
 */
void LORA_sendOK() {
    byte date[] = {0x00, 0x01, 0x01, 0x00, 0x00, 0x00};
    if (isTime()) {
        getPackedTimeBytes(date);
    }
    printCurrentTime();
    printHEX(date, 6);
    byte pac[] = {rxBuffer[1], rxBuffer[2], rxBuffer[3], 0xff,    date[0],
                  date[1],     date[2],     date[3],     date[4], date[5]};
    printHEX(pac, 10);

    LoRa::send(pac, 10);
}

/**
 * @brief Отправляет все пакеты из стека по LoRa с ожиданием подтверждения.
 * @return true если все пакеты отправлены успешно, иначе false.
 * @details Для каждого пакета делает до 2 попыток, ждёт ACK до 5 секунд.
 *          При неудаче возвращает пакет в стек.
 */
bool lora_send() {
    const unsigned long SESSION_TIMEOUT = 1000 * 60;
    const unsigned long ACK_TIMEOUT = 5000;
    const int MAX_ATTEMPTS_PER_PACKET = 2;

    unsigned long sessionStart = millis();
    lora_activate(true);
    Serial.printf("Stack count %i\n", stack.count());

    while (stack.count() > 0) {
        if (millis() - sessionStart > SESSION_TIMEOUT) {
            Serial.println("Session timeout");
            break;
        }

        if (!stack.read(g_packet)) {
            Serial.println("Read failed – stack empty?");
            break;
        }

        Serial.printf(">>> Sending packet [len=%d]\n", g_packet[0]);
        printHEX(g_packet, g_packet[0]);

        bool packetSent = false;
        bool ackReceived = false;

        for (int attempt = 0; attempt < MAX_ATTEMPTS_PER_PACKET && !ackReceived;
             attempt++) {
            if (millis() - sessionStart > SESSION_TIMEOUT)
                break;

            if (!LoRa::send(g_packet, (int)g_packet[0])) {
                Serial.printf("  LoRa send error, attempt %d\n", attempt + 1);
                unsigned long delayMs = random(5000, 25001);
                Serial.printf("  Retry delay: %lu ms\n", delayMs);
                delay(delayMs);
                continue;
            }

            unsigned long ackStart = millis();
            while (millis() - ackStart < ACK_TIMEOUT) {
                int len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
                if (len >= 10 && rxBuffer[3] == 0xFF) {
                    if (rxBuffer[0] == (uint8_t)(ID >> 16) &&
                        rxBuffer[1] == (uint8_t)(ID >> 8) &&
                        rxBuffer[2] == (uint8_t)ID) {
                        ackReceived = true;
                        break;
                    }
                }
                delay(10);
            }

            if (ackReceived) {
                packetSent = true;
                Serial.println("  ACK OK");
                byte dateBytes[6] = {rxBuffer[4], rxBuffer[5], rxBuffer[6],
                                     rxBuffer[7], rxBuffer[8], rxBuffer[9]};
                if (setTimeFromHexBytes(dateBytes))
                    printCurrentTime();
                blink(1, 250);
                break;
            } else {
                Serial.printf("  No ACK, attempt %d\n", attempt + 1);
                unsigned long delayMs = random(5000, 25001);
                Serial.printf("  Retry delay: %lu ms\n", delayMs);
                delay(delayMs);
            }
        }

        if (!packetSent || !ackReceived) {
            if (stack.write(g_packet)) {
                Serial.println("  Returned to stack tail");
            } else {
                Serial.println("  Stack full, packet lost!");
            }
            Serial.println(
                "  Failed to send packet after 2 attempts, ending session.");
            blink(10, 250);
            lora_activate(false);
            return false;
        }

        blink(1, 250);
        delay(100);
    }

    lora_activate(false);
    return true;
}

/**
 * @brief Устанавливает конфигурацию LoRa (канал, адрес) согласно положению переключателей.
 * @details Читает SW1/SW2, вычисляет канал, вызывает configSet().
 */
void lora_setconf() {
    lora_activate(true);
    int ch = readSwitchState() + 1;
    Serial.printf("Chanel set %i\n", ch);
    blink(ch, 400);
    if (LoRa::configSet(17, ch)) {
        LoRa::configGet();
    }
    lora_activate(false);
}

/**
 * @brief Отправляет тестовый пакет LoRa и измеряет RSSI ответа.
 * @param pac указатель на пакет для отправки.
 * @return int RSSI в процентах или -1 при ошибке.
 */
int lora_rssi(byte *pac) {
    int len = 0;
    if (LoRa::send(pac, pac[0])) {
        for (int i = 0; i < 100; i++) {
            len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
            if (len > 0) {
                printHEX(rxBuffer, len);
                if (len > 0 && rxBuffer[3] == 0xff) {
                    if (rxBuffer[0] == (byte)(ID >> 16) & 0xFF &&
                        rxBuffer[1] == (byte)(ID >> 8) & 0xFF &&
                        rxBuffer[2] == (byte)(ID) & 0xFF) {
                        Serial.println(" ID OK");
                        byte dateBytes[6] = {rxBuffer[4], rxBuffer[5],
                                             rxBuffer[6], rxBuffer[7],
                                             rxBuffer[8], rxBuffer[9]};
                        setTimeFromHexBytes(dateBytes);
                        printCurrentTime();
                        blink(1, 2000);
                    } else {
                        blink(10, 250);
                    }
                }
                return rssip(rxBuffer[len - 1]);
            } else {
                Serial.print('.');
            }
            delay(50);
        }
        return -1;
    } else {
        Serial.printf("   ❌ Failed Send)\n");
        return -1;
    }
}

/**
 * @brief Проверяет качество сигнала LoRa и мигает светодиодом.
 */
void lora_check_signal() {
    Serial.printf("\n>>> TEST \n");
    blink(1, 1500);
    byte pac[11] = {};

    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    size_t len = preparePacket(pac, 11, ID, Battery, dateBytes);
    pac[0] = (byte)(len);
    lora_activate(1);
    int rssi = lora_rssi(pac);
    if (rssi == -1) {
        blink(10, 250);
    } else if (rssi <= 20) {
        Serial.printf("rssi %i, 1\n", rssi);
        blink(1, 750);
    } else if (rssi <= 40) {
        Serial.printf("rssi %i, 2\n", rssi);
        blink(2, 750);
    } else if (rssi <= 60) {
        Serial.printf("rssi %i, 3\n", rssi);
        blink(3, 750);
    } else if (rssi <= 80) {
        Serial.printf("rssi %i, 4\n", rssi);
        blink(4, 750);
    } else {
        Serial.printf("rssi %i, 5\n", rssi);
        blink(5, 750);
    }
    lora_activate(false);
}

#endif // NET == 0 || NET == 2

// =====================================================================
// Основные варианты прошивки (setup/loop) в зависимости от конфигурации
// =====================================================================

// Вариант 1: Плата rev3, тип 0, NET == 1 (SIM800)
#if BOARD_REV == 3 && BOARD_TYPE == 0 && NET == 1

void work() {
    if (tableSens[activeport[0]] != 0x00 || tableSens[activeport[1]] != 0x00) {
        dataPrepare();
        enable_sens(0);
        if (stack.count() > 0) {
            if (sim_activate(true)) {
                Serial.println("coneceted do mqtt");
                sim_activate(false);
            }
        }
        enable_power(0);
    } else {
        blink(10, 100);
    }
}

void setup() {
    initPins();
    Serial.begin(115200);
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
    Battery = readBatteryVoltage();

    uint32_t cpu_mhz = getCpuFrequencyMhz();
    uint32_t apb_mhz = getApbFrequency();
    Serial.printf("CPU Frequency: %u MHz\n", cpu_mhz);
    Serial.printf("APB Frequency: %u Hz\n", apb_mhz);

    RsModbus::init(REDE);
    delay(1000);
    if (!loadArrayFromFlash(tableSens)) {
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00;
        }
        saveArrayToFlash(tableSens);
    }

    if (stack.begin()) {
        Serial.printf("✅ Stack initialized. Current records: %d\n",
                      stack.count());
    } else {
        Serial.println("❌ Failed to init FlashStack!");
    }
    loadConfigFromNVS();

    uint8_t wake_but = checkButton();
    Serial.printf("State wake up %i\n\n", wake_but);
    switch (wake_but) {
    case 1:
        work();
        break;
    case 2:
        searchSensors(activeport[0]);
        searchSensors(activeport[1]);
        enable_power(0);
        sleep(10);
        break;
    case 3:
        cleanUpStack();
        searchSensors(activeport[0]);
        searchSensors(activeport[1]);
        SIM_check_signal();
        enable_power(0);
        enable_sim(0);
        sleep(10);
        break;
    default:
        work();
        Serial.println("            complited");
        break;
    }
    sleep(TIME_TO_SLEEP);
}
void loop() {}

// Вариант 2: Плата rev3, тип 0, NET == 0 (LoRa)
#elif BOARD_REV == 3 && BOARD_TYPE == 0 && NET == 0

void setup() {
    initPins();
    Serial.begin(115200);
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }

    Battery = readBatteryVoltage();
    RsModbus::init(REDE);
    delay(1000);
    if (!loadArrayFromFlash(tableSens)) {
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00;
        }
        saveArrayToFlash(tableSens);
    }
    uint8_t wake_but = checkButton();
    if (stack.begin()) {
        Serial.printf("STACK OK, count %i\n", stack.count());
    } else {
        Serial.println("Ошибка: раздел flashbuf не найден!");
    }
    Serial.printf("State wake up %i\n\n", wake_but);
    switch (wake_but) {
    case 1:
        // lora_check_signal();
        // sleep(10);
        break;
    case 2:
        // searchSensors(1);
        // searchSensors(4);
        // sleep(10);
        break;
    case 3:
        Serial.println("work first");
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00;
        }
        saveArrayToFlash(tableSens);
        lora_setconf();
        lora_check_signal();
        searchSensors(1);
        searchSensors(4);
        enable_sens(0);
        enable_power(0);
        saveMSG("wake up");
        sleep(10);
        break;
    default:
        dataPrepare();
        enable_sens(0);
        enable_power(0);
        lora_send();
        Serial.println("            complited");
        break;
    }
    sleep(TIME_TO_SLEEP);
}
void loop() {}

// Вариант 3: Плата rev1, тип 0, NET == 0 (старая плата с LoRa)
#elif BOARD_REV == 1 && BOARD_TYPE == 0 && NET == 0

void setup() {
    initPins();
    Serial.begin(115200);
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
    Battery = readBatteryVoltage();
    delay(1000);
    if (!loadArrayFromFlash(tableSens)) {
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00;
        }
        saveArrayToFlash(tableSens);
    }

    if (stack.begin()) {
        Serial.printf("✅ Stack initialized. Current records: %d\n",
                      stack.count());
    } else {
        Serial.println("❌ Failed to init FlashStack!");
    }

    uint8_t wake_but = checkButton();
    int ch = readSwitchState() + 1;
    Serial.printf("State wake up %i\n\n", wake_but);

    switch (wake_but) {
    case 3:
        Serial.println("work first");
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00;
        }
        saveArrayToFlash(tableSens);
        lora_activate(true);
        Serial.printf("Chanel set %i\n", ch);
        blink(1, 400);
        if (LoRa::configSet(17, ch)) {
            LoRa::configGet();
        }
        blink(ch, 500);
        snprintf(message, sizeof(message),
                 "stack count %d, lora ch %d, signal %d", stack.count(), ch,
                 signalp);
        saveMSG(message);
        lora_check_signal();
        searchSensors(1);
        searchSensors(4);
        enable_sens(0);
        enable_power(0);
        sleep(10);
        break;
    default:
        dataPrepare();
        enable_sens(0);
        enable_power(0);
        lora_send();
        lora_activate(0);
        Serial.println("            complited");
        break;
    }
    sleep(TIME_TO_SLEEP);
}
void loop() {}

// Вариант 4: Плата rev3, тип 1, NET == 2 (LoRa+SIM, шлюз)
#elif BOARD_TYPE == 1 && NET == 2 && BOARD_REV == 3

uint32_t lastWorkTime = 0;
uint32_t lastSleepTime = 0;
uint32_t now = 0;
const uint32_t WORK_INTERVAL_MS = 30UL * 60 * 1000;
const uint32_t SLEEP_INTERVAL_MS = 135UL * 60 * 1000;

void work() {
    if (sim_activate(true)) {
        printCurrentTime();
        Serial.println("    TRY TO DO HTTP SEND");
        if (http_send()) {
            Serial.println("    ✅ SENDING COMPLETE");
        } else {
            Serial.println("   ❌ SENDING FAIL");
            saveMSG("ERROR with sending http");
        }
    } else {
        Serial.println("   ❌ SIM activation failed");
    }
    if (sim_activate(false)) {
        Serial.println("work end correct");
    }
}

void setup() {
    initPins();
    Serial.begin(115200);
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Battery = readBatteryVoltage();
    if (stack.begin()) {
        Serial.printf("✅ Stack initialized. Current records: %d\n",
                      stack.count());
    } else {
        Serial.println("❌ Failed to init FlashStack!");
    }
    loadConfigFromNVS();
    int wakebut = checkButton();
    Serial.printf("     STATE WAKE UP %i\n", wakebut);
    if (wakebut == 3) {
        SIM_check_signal();
        lora_setconf();
    }
    snprintf(message, sizeof(message), "stack count %d, signal %d",
             stack.count(), signalp);
    saveMSG(message);
    saveMSG("wake up");
    uint32_t now = millis();
    if (lastWorkTime == 0)
        lastWorkTime = now;
    if (lastSleepTime == 0)
        lastSleepTime = now;
    blink(3, 333);
    Serial.println("        work start");
    lora_activate(true);
}

void loop() {
    // Приём LoRa-пакетов
    int len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
    if (len > 0) {
        digitalWrite(LED_PIN, HIGH);
        Serial.printf("\n\n📨 Packet %d bytes\n", len);
        printHEX(rxBuffer, (int)rxBuffer[0] + 1);
        byte senstime[] = {rxBuffer[5], rxBuffer[6], rxBuffer[7],
                           rxBuffer[8], rxBuffer[9], rxBuffer[10]};
        printTimeFromHexBytes(senstime);
        bool crcOK = checkCRC(rxBuffer, (int)rxBuffer[0]);
        bool itsMSG = false;
        if (rxBuffer[4] == 0xff && rxBuffer[5] == 0xff &&
            rxBuffer[6] == 0xff) {
            itsMSG = true;
        }
        if (len > 15 && (crcOK || itsMSG)) {
            Serial.println("✅Valid sensor data");
            uint8_t *packet = g_packet;
            memset(packet, 0, sizeof(g_packet));
            memcpy(packet, rxBuffer, len);
            if (stack.write(packet)) {
                LORA_sendOK();
                Serial.printf("pushed len %i, count %i\n", len, stack.count());
            }
        }
        if (!crcOK && len < 15) {
            Serial.println("📣Sensor signal check");
            LORA_sendOK();
        }
        digitalWrite(LED_PIN, LOW);
    }

    now = millis();
    bool workExpired =
        (lastWorkTime == 0) || (now - lastWorkTime >= WORK_INTERVAL_MS);
    if (stack.count() > 0 && workExpired) {
        lora_activate(false);
        blink(2, 750);
        Serial.printf("stack count %d, workExpired %d\n", stack.count(),
                      workExpired);
        work();
        lastWorkTime = millis();
        blink(2, 750);
        lora_activate(true);
    }

    bool sleepExpired =
        (lastSleepTime == 0) || (now - lastSleepTime >= SLEEP_INTERVAL_MS);
    if (sleepExpired) {
        lastSleepTime = millis();
        Serial.println("Entering deep sleep for 10 seconds (resetmodules)...");
        Serial.flush();
        lora_activate(false);
        sim_activate(false);
        sleep(10);
    }
    delay(10);
    yield();
}

// Вариант 5: Плата rev3, тип 2 (WiFi + LoRa для отладки)
#elif BOARD_REV == 3 && BOARD_TYPE == 2

#if BOARD_TYPE == 2
auto &RealSerial = Serial;
String logBuf;
const size_t MAX_LOG = 3000;
const size_t TRIM_SIZE = 300;

class DualSerial : public Print {
  public:
    void begin(unsigned long baud = 115200) { RealSerial.begin(baud); }
    void end() { RealSerial.end(); }
    int available() { return RealSerial.available(); }
    int read() { return RealSerial.read(); }
    int peek() { return RealSerial.peek(); }
    void flush() { RealSerial.flush(); }
    operator bool() { return (bool)RealSerial; }

    size_t write(uint8_t c) override {
        RealSerial.write(c);
        while (logBuf.length() + 1 > MAX_LOG)
            logBuf.remove(0, TRIM_SIZE);
        logBuf += (char)c;
        return 1;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        RealSerial.write(buffer, size);
        while (logBuf.length() + size > MAX_LOG)
            logBuf.remove(0, TRIM_SIZE);
        logBuf.concat((const char *)buffer, size);
        return size;
    }
    using Print::print;
    using Print::printf;
    using Print::println;
};

DualSerial Dual;
#undef Serial
#define Serial Dual

AsyncWebServer server(80);
const char *ssidwifi = "S2-Debug";
const char *passwifi = "12345678";

void setup() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(ELORA, OUTPUT);
    pinMode(BUT2, INPUT_PULLUP);
    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
    Serial.begin(115200);
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }

    lora_activate(true);
    int ch = readSwitchState() + 1;
    Serial.printf("Chanel set %i\n", ch);
    blink(ch, 400);
    if (LoRa::configSet(17, ch)) {
        LoRa::configGet();
    }
    lora_activate(0);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssidwifi, passwifi);
    delay(1000);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", R"rawliteral(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>
    body{margin:0;background:#0d1117;color:#c9d1d9;font:16px/1.4
    monospace;height:100dvh;display:flex;flex-direction:column}
    #log{flex:1;overflow:auto;padding:8px;white-space:pre-wrap}
    #st{padding:6px;background:#161b22;border-bottom:1px solid
    #30363d;font-size:14px}
    </style></head><body>
    <div id="st">Loading...</div><div id="log"></div>
    <script>
    const el=document.getElementById('log'),
    st=document.getElementById('st'); async function poll(){
      try{
        const r=await fetch('/log');
        if(!r.ok) throw 1;
        const t=await r.text();
        if(el.textContent!==t){el.textContent=t;el.scrollTop=el.scrollHeight;}
        st.textContent='● '+t.length+'B';
        setTimeout(poll,400);
      }catch(e){st.textContent='○ Error';setTimeout(poll,1000);}
    }
    poll();
    </script></body></html>
    )rawliteral");
    });

    server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response =
            request->beginResponse(200, "text/plain", logBuf);
        response->addHeader("Cache-Control",
                            "no-store, no-cache, must-revalidate");
        request->send(response);
    });

    server.begin();
    Serial.println("Ready: http://192.168.4.1");
    lora_activate(true);
}

int i = 0;
float bytesToInt(uint8_t high, uint8_t low) { return ((int)high << 8) | low; }

void loop() {
    if (digitalRead(BUT2) == LOW) {
        lora_check_signal();
    }
    delay(1000);
}

#endif // BOARD_TYPE == 2

#endif // main configs