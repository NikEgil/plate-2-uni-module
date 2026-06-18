// =====================================================================
//  sim.cpp – Модуль управления SIM800 (GSM/GPRS) для ESP32
// =====================================================================
//  Использует библиотеку TinyGSM для работы с модемом через HardwareSerial.
//  Предоставляет функции для:
//    - инициализации и деинициализации
//    - подключения к GPRS (APN)
//    - получения времени сети (AT+CCLK)
//    - отправки HTTP-запросов (POST) с автоматическим восстановлением
//    - чтения/удаления SMS
//    - управления питанием и сбросом модема
//  Весь код компилируется только при NET > 0.
// =====================================================================

#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_DEBUG Serial
#include "sim.h"
#include <SoftwareSerial.h>
#include <TinyGsmClient.h>
#include <defenitions.h>
#include <esp_task_wdt.h>
#include <string.h>

#if NET > 0

// =====================================================================
// Внутренние глобальные переменные (анонимный namespace)
// =====================================================================
namespace {
    HardwareSerial *simSerial = nullptr;   ///< Указатель на UART (используется Serial1)
    TinyGsm *modem = nullptr;              ///< Объект модема TinyGSM
    TinyGsmClient *client = nullptr;       ///< TCP-клиент для HTTP
    bool isActive = false;                 ///< Флаг активности модема (питание включено и UART открыт)
    bool isGprsConnected = false;          ///< Флаг подключения к GPRS (интернет)
}

// =====================================================================
// Пространство имён SimModule – все публичные функции
// =====================================================================
namespace SimModule {

// =====================================================================
// begin() – Инициализация модема
// =====================================================================
/**
 * @brief Инициализирует модем SIM800 через HardwareSerial (UART1).
 * @param rxPin  пин RX (если -1, используется SIMRX из defenitions.h)
 * @param txPin  пин TX (если -1, используется SIMTX)
 * @param baud   скорость (если 0, используется SIM_BAUD)
 * @details Создаёт объекты TinyGsm и TinyGsmClient, настраивает UART.
 *          Отключает все лишние URC (сообщения модема), эхо,
 *          уведомления о SMS, регистрации в сети, таймауты TCP/IP.
 *          Сохраняет настройки командой AT&W.
 *          В случае успеха isActive = true.
 */
void begin(int rxPin, int txPin, uint32_t baud) {
    end(); // Освобождаем предыдущие ресурсы
    if (rxPin == -1) rxPin = SIMRX;
    if (txPin == -1) txPin = SIMTX;
    if (baud == 0) baud = SIM_BAUD;
    Serial.println("\n=== [SIM] begin() (HardwareSerial) ===");
    Serial.printf("[SIM] Parameters: RX=%d, TX=%d, Baud=%lu\n", rxPin, txPin, baud);

    // Используем Serial1 (аппаратный UART)
    simSerial = &Serial1;
    simSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
    simSerial->setTimeout(1000);

    modem = new TinyGsm(*simSerial);
    client = new TinyGsmClient(*modem);

    // Лямбда для очистки буфера модема (чтение всего, что пришло)
    auto flush = [&]() {
        while (simSerial->available()) {
            String junk = simSerial->readStringUntil('\n');
            if (junk.length()) Serial.print("[FLUSH] ");
            Serial.println(junk);
        }
    };

    // Отключаем эхо команд
    modem->sendAT(GF("E0"));
    modem->waitResponse(500);
    flush();

    // Устанавливаем текстовый режим SMS (необязательно, но оставим)
    modem->sendAT(GF("+CMGF=1"));
    modem->waitResponse(500);
    flush();

    // Отключаем уведомления о новых SMS (CNMI)
    modem->sendAT(GF("+CNMI=0,0,0,0,0"));
    modem->waitResponse(500);
    flush();

    // Отключаем URC о регистрации в сети (CREG)
    modem->sendAT(GF("+CREG=0"));
    modem->waitResponse(500);
    flush();

    // Отключаем URC о GPRS-регистрации (CGREG)
    modem->sendAT(GF("+CGREG=0"));
    modem->waitResponse(500);
    flush();

    // Отключаем slow clock (энергосбережение) – нужно для стабильной работы
    modem->sendAT(GF("+CSCLK=0"));
    modem->waitResponse(500);
    flush();

    // ОТКЛЮЧАЕМ ВСЕ URC ДЛЯ TCP/IP (важно!)
    // Отключаем автоматические уведомления о приёме данных
    modem->sendAT(GF("+CIPRXGET=0"));
    modem->waitResponse(500);
    flush();

    // Добавлять IP-заголовок (не влияет на URC)
    modem->sendAT(GF("+CIPHEAD=1"));
    modem->waitResponse(500);
    flush();

    // Отключаем автоматический вывод статуса сокета
    modem->sendAT(GF("+CIPSTATUS=0"));
    modem->waitResponse(500);
    flush();

    // Устанавливаем таймауты TCP (20 секунд на соединение, отправку, приём)
    modem->sendAT(GF("+CIPTIMEOUT=20000,20000,20000"));
    modem->waitResponse(1000);
    flush();

    // Сохраняем настройки в энергонезависимую память модема
    modem->sendAT(GF("&W"));
    modem->waitResponse(1000);
    flush();

    Serial.println("=== [SIM] begin() SUCCESS ===");
}

// =====================================================================
// end() – Деинициализация и освобождение ресурсов
// =====================================================================
/**
 * @brief Освобождает все ресурсы: удаляет объекты, закрывает UART.
 * @details Сбрасывает флаги isActive и isGprsConnected.
 *          Вызывается перед повторной инициализацией или при завершении работы.
 */
void end() {
    Serial.println("[SIM] end() – releasing resources");
    if (client) {
        delete client;
        client = nullptr;
    }
    if (modem) {
        delete modem;
        modem = nullptr;
    }
    if (simSerial) {
        simSerial->end(); // отключает UART и освобождает пины
        simSerial = nullptr;
    }
    isActive = false;
    isGprsConnected = false;
}

// =====================================================================
// activate() – Включение/выключение модема (логическое)
// =====================================================================
/**
 * @brief Активирует или деактивирует модем (управление питанием на уровне ПО).
 * @param act true – включить (isActive = true), false – выключить.
 * @details При выключении (act=false) разрывает GPRS-соединение.
 *          Физическое питание модема не управляется – это делается внешними функциями (enable_sim).
 *          Данная функция лишь устанавливает флаг, разрешающий операции с модемом.
 */
void activate(bool act) {
    Serial.println("\n=== [SIM] activate() START ===");
    Serial.printf("[SIM] Requested state: %s\n", act ? "ON" : "OFF");
    if (act && !isActive) {
        if (!simSerial) {
            Serial.println("[SIM] ERROR: simSerial is NULL! Call begin() first!");
            return;
        }
        isActive = true;
        Serial.println("[SIM] activate() = TRUE");
    } else if (!act && isActive) {
        if (isGprsConnected) {
            modem->gprsDisconnect();
            isGprsConnected = false;
            Serial.println("[SIM] GPRS disconnected");
        }
        isActive = false;
        Serial.println("[SIM] activate() = FALSE");
    } else {
        Serial.printf("[SIM] Already in requested state: isActive=%d\n", isActive);
    }
    delay(100);
}

// =====================================================================
// connect() – Подключение к GPRS
// =====================================================================
/**
 * @brief Устанавливает GPRS-соединение с заданным APN.
 * @param apn  имя точки доступа (APN)
 * @param user логин (если требуется)
 * @param pass пароль
 * @return true если удалось подключиться к сети и GPRS, иначе false.
 * @details Сначала ожидает регистрации в сети (до 3 попыток по 15 секунд),
 *          затем подключается к GPRS (также до 3 попыток).
 *          Сбрасывает сторожевой таймер во время ожидания.
 *          При успехе выводит уровень сигнала и локальный IP.
 */
bool connect(const char *apn, const char *user, const char *pass) {
    if (!isActive) {
        Serial.println("SIM not activated");
        return false;
    }
    Serial.print(F("Modem: "));
    Serial.println(modem->getModemName());
    esp_task_wdt_init(120, false); // увеличиваем таймаут WDT на время долгих операций
    yield();
    Serial.println(F("Waiting for network..."));
    bool networkReady = false;
    for (int i = 1; i <= 3; i++) {
        Serial.printf("Try %d/2... ", i);
        unsigned long start = millis();
        while (millis() - start < 15000) {
            yield();
            esp_task_wdt_reset(); // сброс программного WDT
            if (modem->isNetworkConnected()) {
                networkReady = true;
                break;
            }
            delay(100);
        }
        if (networkReady) {
            Serial.println(F("OK"));
            break;
        }
        if (i == 3) {
            Serial.println(F("Failed"));
            esp_task_wdt_init(8, true); // восстанавливаем стандартный WDT
            return false;
        }
        delay(200);
    }
    Serial.println(F("Connecting to GPRS..."));
    for (int i = 1; i <= 3; i++) {
        Serial.printf("Try %d/2... ", i);
        yield();
        esp_task_wdt_reset();
        if (modem->gprsConnect(apn, user, pass)) {
            Serial.println(F("OK"));
            isGprsConnected = true;
            break;
        }
        if (i == 3) {
            Serial.println(F("Failed"));
            esp_task_wdt_init(8, true);
            return false;
        }
        delay(200);
        yield();
    }
    esp_task_wdt_init(8, true);
    yield();
    delay(200);
    Serial.printf("Signal: %d\n", getSignalQuality());
    Serial.printf("IP: %s\n", getLocalIP().c_str());
    return true;
}

// =====================================================================
// disconnect() – Разрыв GPRS
// =====================================================================
/**
 * @brief Разрывает GPRS-соединение (AT+CIPSHUT).
 * @details Если соединение активно, отправляет команду +CIPSHUT для принудительного закрытия.
 *          Сбрасывает флаг isGprsConnected.
 */
void disconnect() {
    if (!isGprsConnected) {
        Serial.println("⚠️ GPRS already disconnected");
        return;
    }
    Serial.println("📡 SimModule: Disconnecting GPRS...");
    if (modem->isGprsConnected()) {
        Serial.println("⚠️ GPRS disconnect forcing...");
        modem->sendAT("+CIPSHUT");
        modem->waitResponse(2000);
        delay(300);
    }
    isGprsConnected = false;
    Serial.println("✅ GPRS disconnected");
}

// =====================================================================
// factoryReset() – Сброс модема к заводским настройкам
// =====================================================================
/**
 * @brief Выполняет полный сброс модема: AT&F, затем AT+CFUN=1,1 (перезагрузка).
 * @return true если удалось перезагрузить и получить ответ "AT", иначе false.
 * @details После сброса ожидает до 15 секунд, пока модем не ответит на "AT".
 *          Сбрасывает флаг isGprsConnected.
 */
bool factoryReset() {
    if (!modem || !isActive) {
        Serial.println("❌ Modem not active or initialized");
        return false;
    }
    Serial.println("🔄 SIM800: Starting factory reset...");
    esp_task_wdt_init(30, false);
    yield();
    bool success = true;
    Serial.println("  → AT&F (Factory defaults)...");
    String ans;
    if (modem->factoryDefault()) {
        if (modem->waitResponse(3000, ans) != 1) {
            Serial.println("  ⚠️ AT&F timeout/failed");
            success = false;
        }
        Serial.println(ans);
    }
    delay(500);
    Serial.println("  → AT+CFUN=1,1 (Reboot)...");
    modem->sendAT(GF("+CFUN=1,1"));
    Serial.println("  → Waiting for reboot completion...");
    unsigned long start = millis();
    bool isReady = false;
    while (millis() - start < 15000) {
        yield();
        esp_task_wdt_reset();
        modem->sendAT("AT");
        if (modem->waitResponse(1000) == 1) {
            isReady = true;
            break;
        }
        delay(500);
    }
    if (isReady) {
        Serial.println("✅ SIM800 factory reset & reboot OK");
    } else {
        Serial.println("❌ SIM800 did not respond after reboot");
        success = false;
    }
    esp_task_wdt_init(8, true);
    yield();
    isGprsConnected = false;
    return success;
}

// =====================================================================
// ccid() – Получение ICCID (идентификатор SIM-карты)
// =====================================================================
/**
 * @brief Читает ICCID через modem->getSimCCID() и упаковывает в BCD-формат.
 * @param ccid выходной массив из 10 байт (20-значный номер ICCID).
 * @return int всегда 10 (размер массива).
 * @details Преобразует строку (например, "894...") в BCD (каждая пара цифр в байт).
 *          Если длина меньше 20, недостающие символы заменяются на '0'.
 */
int ccid(byte *ccid) {
    String iccid = modem->getSimCCID();
    Serial.println(iccid);
    size_t len = iccid.length();
    memset(ccid, 0, 10);
    for (size_t i = 0; i < 10; i++) {
        char c1 = (i * 2 < len) ? iccid[i * 2] : '0';
        char c2 = (i * 2 + 1 < len) ? iccid[i * 2 + 1] : '0';
        uint8_t high = (c1 >= '0' && c1 <= '9') ? c1 - '0' : 0;
        uint8_t low = (c2 >= '0' && c2 <= '9') ? c2 - '0' : 0;
        ccid[i] = (high << 4) | low;
    }
    return 10;
}

// =====================================================================
// AT() – Отправка произвольной AT-команды
// =====================================================================
/**
 * @brief Отправляет произвольную AT-команду модему и возвращает ответ.
 * @param cmd         команда без префикса "AT" (будет добавлен автоматически)
 * @param outResponse ссылка на строку, куда будет записан ответ модема
 * @return true если ответ содержит "OK" (status=1), иначе false.
 * @details Использует modem->waitResponse() с таймаутом 3000 мс.
 *          Выводит команду и ответ в Serial для отладки.
 */
bool AT(const String &cmd, String &outResponse) {
    if (!modem) {
        Serial.println("❌ Modem not initialized");
        return false;
    }
    Serial.printf("📡 AT: %s\n", cmd.c_str());
    modem->sendAT(cmd);
    int8_t status = modem->waitResponse(3000, outResponse);
    Serial.printf("DEBUG AT: cmd=%s, status=%d, raw='%s'\n", cmd.c_str(), status, outResponse.c_str());
    if (status == 1) {
        Serial.println("✅ OK");
        Serial.println(outResponse);
        return true;
    } else if (status == 2) {
        Serial.println("❌ ERROR from modem");
    } else {
        Serial.println("⏱️ Timeout waiting for response");
    }
    Serial.printf("📥 Raw: [%s]\n", outResponse.c_str());
    return false;
}

// =====================================================================
// isConnection() – Проверка активности GPRS и модема
// =====================================================================
bool isConnection() { return isGprsConnected && isActive; }

// =====================================================================
// hasNetwork() – Проверка регистрации в сети
// =====================================================================
bool hasNetwork() { return isActive && modem->isNetworkConnected(); }

// =====================================================================
// getSignalQuality() – Уровень сигнала в процентах
// =====================================================================
/**
 * @brief Возвращает качество сигнала в процентах (0..100).
 * @return int -1 если модем неактивен, иначе (RSSI/31)*100.
 */
int getSignalQuality() {
    return isActive ? modem->getSignalQuality() * 100 / 31 : -1;
}

// =====================================================================
// getLocalIP() – Локальный IP-адрес
// =====================================================================
String getLocalIP() { return isActive ? modem->getLocalIP() : ""; }

// =====================================================================
// getModemInfo() – Название модема (модель)
// =====================================================================
String getModemInfo() { return isActive ? modem->getModemName() : ""; }

// =====================================================================
// getClient() – Получение указателя на объект клиента (для низкоуровневого доступа)
// =====================================================================
void *getClient() { return static_cast<void *>(client); }

// =====================================================================
// enableTimeSync() – Включение синхронизации времени с сетью
// =====================================================================
/**
 * @brief Включает автоматическую синхронизацию времени модема по сети (AT+CLTS=1).
 * @return true если команда выполнена успешно и сохранена.
 */
bool enableTimeSync() {
    if (!isActive || !modem) return false;
    Serial.println("[SIM] Enabling network time sync...");
    modem->sendAT(GF("+CLTS=1"));
    if (modem->waitResponse(1000) != 1) {
        Serial.println("[SIM] Failed to set AT+CLTS=1");
        return false;
    }
    modem->sendAT(GF("&W"));
    modem->waitResponse(1000);
    Serial.println("[SIM] Time sync enabled");
    return true;
}

// =====================================================================
// getNetworkTime() – Получение сетевого времени (AT+CCLK)
// =====================================================================
/**
 * @brief Запрашивает текущее время сети через AT+CCLK и возвращает UTC-время.
 * @return NetTime структура с полями year, month, day, hour, minute, second, timezone (всегда 0) и флаг valid.
 * @details Парсит ответ модема вида "+CCLK: "YY/MM/DD,HH:MM:SS+TZ"".
 *          Преобразует локальное время сети с учётом часового пояса в UTC.
 *          Для преобразования временно устанавливает переменную окружения TZ в соответствии со смещением.
 *          Затем через mktime() и gmtime_r() получает UTC.
 *          Если разбор или преобразование не удались, возвращает valid=false.
 */
NetTime getNetworkTime() {
    NetTime result = {0};
    result.valid = false;

    if (!isActive || !modem) return result;

    modem->sendAT(GF("+CCLK?"));
    String res;
    int8_t status = modem->waitResponse(5000, res);
    if (status != 1) {
        Serial.printf("[SIM] getNetworkTime: no response (status=%d)\n", status);
        return result;
    }

    int cclkPos = res.indexOf("+CCLK: ");
    if (cclkPos < 0) {
        Serial.println("[SIM] getNetworkTime: '+CCLK: ' not found");
        return result;
    }

    int q1 = res.indexOf('"', cclkPos);
    if (q1 < 0) {
        Serial.println("[SIM] getNetworkTime: opening quote not found");
        return result;
    }
    int q2 = res.indexOf('"', q1 + 1);
    if (q2 < 0) {
        Serial.println("[SIM] getNetworkTime: closing quote not found");
        return result;
    }

    String timeStr = res.substring(q1 + 1, q2);
    // Ожидаемый формат: "YY/MM/DD,HH:MM:SS+TZ" (пример: "25/05/14,12:30:45+12")
    int year, month, day, hour, minute, second, tz;
    char tzSign;
    int fields = sscanf(timeStr.c_str(), "%d/%d/%d,%d:%d:%d%c%d", &year, &month,
                        &day, &hour, &minute, &second, &tzSign, &tz);

    if (fields < 8) {
        Serial.printf("[SIM] getNetworkTime: sscanf failed (got %d fields) from '%s'\n",
                      fields, timeStr.c_str());
        return result;
    }

    if (tzSign == '-') tz = -tz; // tz в четвертях часа
    if (year < 100) year += 2000;

    // Проверка разумности
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 ||
        hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        Serial.printf("[SIM] getNetworkTime: invalid date/time: %04d-%02d-%02d "
                      "%02d:%02d:%02d\n", year, month, day, hour, minute, second);
        return result;
    }

    // Смещение в минутах: каждая четверть часа = 15 минут
    int offset_minutes = tz * 15; // положительное, если локальное время впереди UTC

    // Формируем struct tm из локального времени сети
    struct tm t_local = {0};
    t_local.tm_year = year - 1900;
    t_local.tm_mon = month - 1;
    t_local.tm_mday = day;
    t_local.tm_hour = hour;
    t_local.tm_min = minute;
    t_local.tm_sec = second;
    t_local.tm_isdst = -1; // неизвестно

    // Сохраняем текущую зону, чтобы восстановить позже
    const char *old_tz = getenv("TZ");
    char old_tz_buf[32] = {0};
    if (old_tz) {
        strncpy(old_tz_buf, old_tz, sizeof(old_tz_buf) - 1);
    } else {
        strcpy(old_tz_buf, "UTC0");
    }

    // Устанавливаем временную зону так, чтобы mktime() интерпретировал t_local
    // как время с полученным смещением.
    // Формат POSIX: "UTC-3" означает, что локальное время = UTC + 3 часа.
    if (offset_minutes == 0) {
        setenv("TZ", "UTC0", 1);
    } else if (offset_minutes > 0) {
        char buf[20];
        snprintf(buf, sizeof(buf), "UTC-%d:%02d", offset_minutes / 60, offset_minutes % 60);
        setenv("TZ", buf, 1);
    } else { // offset_minutes < 0
        int pos = -offset_minutes;
        char buf[20];
        snprintf(buf, sizeof(buf), "UTC+%d:%02d", pos / 60, pos % 60);
        setenv("TZ", buf, 1);
    }
    tzset();

    // Получаем абсолютное время (UTC) через mktime
    time_t utc_timestamp = mktime(&t_local);

    // Восстанавливаем прежнюю временную зону
    setenv("TZ", old_tz_buf, 1);
    tzset();

    if (utc_timestamp == (time_t)-1) {
        Serial.println("[SIM] getNetworkTime: mktime() failed");
        return result;
    }

    // Разбираем UTC timestamp обратно в календарные поля
    struct tm utc_tm;
    if (gmtime_r(&utc_timestamp, &utc_tm) == nullptr) {
        Serial.println("[SIM] getNetworkTime: gmtime_r() failed");
        return result;
    }

    // Заполняем результат полями UTC, timezone = 0
    result.year = utc_tm.tm_year + 1900;
    result.month = utc_tm.tm_mon + 1;
    result.day = utc_tm.tm_mday;
    result.hour = utc_tm.tm_hour;
    result.minute = utc_tm.tm_min;
    result.second = utc_tm.tm_sec;
    result.timezone = 0;
    result.valid = true;
    return result;
}

// =====================================================================
// Вспомогательная функция: makeUtcTime()
// =====================================================================
/**
 * @brief Создаёт time_t (Unix timestamp) из UTC-полей.
 * @param year, month, day, hour, minute, second – значения в UTC.
 * @return time_t или -1 при ошибке.
 * @details Временно устанавливает TZ=UTC0, вызывает mktime(), восстанавливает TZ.
 */
static time_t makeUtcTime(int year, int month, int day, int hour, int minute, int second) {
    // Сохраняем текущий TZ
    const char *old_tz = getenv("TZ");
    char old_tz_buf[32] = {0};
    if (old_tz) {
        strncpy(old_tz_buf, old_tz, sizeof(old_tz_buf) - 1);
    } else {
        strcpy(old_tz_buf, "UTC0");
    }

    setenv("TZ", "UTC0", 1);
    tzset();

    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;
    time_t ts = mktime(&t);

    // Восстанавливаем
    setenv("TZ", old_tz_buf, 1);
    tzset();

    return ts;
}

// =====================================================================
// syncSystemClock() – Синхронизация системного времени ESP32
// =====================================================================
/**
 * @brief Получает сетевое время и устанавливает системные часы ESP32 (settimeofday).
 * @return 1 если успешно, 0 если не удалось, -1 если прошло менее 3 секунд с последней попытки.
 * @details Вызывается не чаще раза в 3 секунды. Если GPRS не активен – возвращает 0.
 *          При успехе выводит сообщение о синхронизации.
 */
int syncSystemClock() {
    static uint32_t lastAttempt = 0;
    uint32_t now = millis();
    if (now - lastAttempt < 3000) {
        return -1;
    }
    lastAttempt = now;

    if (!modem || !modem->isGprsConnected()) {
        Serial.println("[SIM] GPRS not connected");
        return 0;
    }

    NetTime nt = getNetworkTime();
    if (!nt.valid || nt.year < 2020 || nt.year > 2050) {
        Serial.printf("[SIM] Invalid time: %04d-%02d-%02d\n", nt.year, nt.month, nt.day);
        return 0;
    }

    time_t ts = makeUtcTime(nt.year, nt.month, nt.day, nt.hour, nt.minute, nt.second);
    if (ts == (time_t)-1) {
        Serial.println("[SIM] makeUtcTime() failed");
        return 0;
    }

    struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
    if (settimeofday(&tv, nullptr) != 0) {
        Serial.println("[SIM] settimeofday() failed");
        return 0;
    }

    Serial.printf("[SIM] ✓ Synced UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  nt.year, nt.month, nt.day, nt.hour, nt.minute, nt.second);
    return 1;
}

// =====================================================================
// getTimestamp() – Получение Unix-времени из сети
// =====================================================================
/**
 * @brief Возвращает текущий Unix timestamp (UTC) из сети.
 * @return time_t или 0, если время невалидно.
 */
time_t getTimestamp() {
    NetTime nt = getNetworkTime();
    if (!nt.valid) return 0;
    return makeUtcTime(nt.year, nt.month, nt.day, nt.hour, nt.minute, nt.second);
}

// =====================================================================
// getUnreadSMS() – Чтение первого непрочитанного SMS
// =====================================================================
/**
 * @brief Запрашивает список непрочитанных SMS (AT+CMGL="REC UNREAD").
 * @return String – текст первого найденного SMS (без заголовков), или пустую строку, если нет.
 * @details Парсит ответ модема, извлекает индекс сообщения, затем текст.
 *          После извлечения удаляет это SMS (AT+CMGD).
 *          Временно переключает режим SMS в текстовый, если не был.
 */
String getUnreadSMS() {
    if (!modem || !isActive) return "";

    // Запросить список непрочитанных
    modem->sendAT(GF("+CMGL=\"REC UNREAD\""));
    String response;
    if (modem->waitResponse(5000, response) != 1) {
        modem->sendAT(GF("+CMGF=0")); // возвращаем PDU режим (если был)
        modem->waitResponse(500);
        return "";
    }
    // Найти первый блок +CMGL: ...
    int pos = response.indexOf("+CMGL:");
    if (pos == -1) {
        return ""; // нет непрочитанных
    }
    // Индекс SMS (не обязателен для возврата, но пригодится при удалении)
    int idx1 = response.indexOf(':', pos);
    int idx2 = response.indexOf(',', idx1);
    int smsIndex = (idx1 != -1 && idx2 != -1) ? response.substring(idx1 + 1, idx2).toInt() : -1;
    // Текст сообщения – начинается со следующей строки после заголовка
    int lineEnd = response.indexOf('\n', pos);
    if (lineEnd == -1) lineEnd = response.length();
    String text;
    int textPos = lineEnd + 1;
    while (textPos < response.length()) {
        int nextLineEnd = response.indexOf('\n', textPos);
        if (nextLineEnd == -1) nextLineEnd = response.length();
        String nextLine = response.substring(textPos, nextLineEnd);
        nextLine.trim();
        if (nextLine.startsWith("+CMGL:") || nextLine == "OK") break;
        if (text.length() > 0) text += "\n";
        text += nextLine;
        textPos = nextLineEnd + 1;
    }
    // Удаляем это SMS
    modem->sendAT("+CMGD=" + String(smsIndex));
    modem->waitResponse(1000);
    return text;
}

// =====================================================================
// deleteSMS() – Удаление SMS по индексу
// =====================================================================
bool deleteSMS(int index) {
    if (!modem) return false;
    String cmd = "+CMGD=" + String(index);
    modem->sendAT(cmd);
    int8_t res = modem->waitResponse(1000);
    return (res == 1);
}

// =====================================================================
// stop() – Принудительное закрытие TCP-соединения клиента
// =====================================================================
void stop() {
    if (client->connected()) {
        Serial.println("[HTTP] Closing previous connection");
        client->stop();
        delay(200);
    }
}

// =====================================================================
// Внутренние переменные состояния HTTP
// =====================================================================
static bool _httpConnected = false;   ///< Флаг, что TCP-соединение установлено
static String _httpHost;              ///< Хост для текущего HTTP-соединения
static int _httpPort;                 ///< Порт для текущего HTTP-соединения

// =====================================================================
// clearModemBuffer() – Очистка входного буфера модема
// =====================================================================
/**
 * @brief Читает и выбрасывает все данные из UART модема в течение 500 мс.
 * @details Используется для удаления случайных URC или остаточных данных после команд.
 */
static void clearModemBuffer() {
    unsigned long start = millis();
    while (millis() - start < 500) {
        if (simSerial->available()) {
            String line = simSerial->readStringUntil('\n');
            if (line.length()) {
                Serial.print("[CLEAR] ");
                Serial.println(line);
            }
        } else {
            delay(5);
        }
    }
    // Добиваем всё, что осталось
    while (simSerial->available()) {
        simSerial->read();
    }
}

// =====================================================================
// readFullHttpResponse() – Чтение полного HTTP-ответа
// =====================================================================
/**
 * @brief Читает HTTP-ответ от сервера до конца (с учётом Content-Length).
 * @param outCode ссылка для сохранения HTTP-кода ответа (например, 200).
 * @return true если ответ получен полностью и код 200 или 202, иначе false.
 * @details Парсит заголовки, определяет Content-Length, читает тело, игнорирует его.
 *          После чтения очищает буфер модема.
 */
static bool readFullHttpResponse(int &outCode) {
    outCode = -1;
    if (!client || !client->connected()) return false;

    unsigned long timeout = millis() + 20000;
    int contentLength = -1;
    bool headersComplete = false;

    // Читаем заголовки построчно
    while (!headersComplete && millis() < timeout) {
        if (!client->available()) {
            delay(10);
            yield();
            continue;
        }
        String line = client->readStringUntil('\n');
        line.trim();

        if (line.startsWith("HTTP/") && outCode == -1) {
            int sp1 = line.indexOf(' ');
            int sp2 = line.indexOf(' ', sp1 + 1);
            if (sp1 != -1 && sp2 != -1) {
                outCode = line.substring(sp1 + 1, sp2).toInt();
            }
        }

        if (line.length() == 0) {
            headersComplete = true;
        }
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
        }
    }

    if (!headersComplete) {
        Serial.println("[HTTP] Headers timeout");
        return false;
    }

    // Читаем тело ответа, если есть Content-Length
    if (contentLength > 0) {
        uint8_t buf[256];
        int totalRead = 0;
        while (totalRead < contentLength && millis() < timeout) {
            if (client->available()) {
                int toRead = min((int)sizeof(buf), contentLength - totalRead);
                int r = client->read(buf, toRead);
                if (r > 0) totalRead += r;
            } else {
                delay(10);
                yield();
            }
        }
        if (totalRead < contentLength) {
            Serial.printf("[HTTP] Incomplete body: %d/%d\n", totalRead, contentLength);
            return false;
        }
    }

    // Дополнительная очистка буфера модема после ответа
    clearModemBuffer();

    // Успех, если код 200 или 202
    return (outCode == 200 || outCode == 202);
}

// =====================================================================
// httpBegin() – Установка TCP-соединения для HTTP
// =====================================================================
/**
 * @brief Устанавливает TCP-соединение с заданным хостом и портом.
 * @param host строка с именем хоста или IP
 * @param port номер порта
 * @return true если соединение установлено, иначе false.
 * @details Если уже есть активное соединение, проверяет его живость через AT+CIPSTATUS.
 *          При невалидном состоянии закрывает и создаёт заново.
 *          Очищает буфер после соединения.
 */
bool httpBegin(const char *host, int port) {
    if (_httpConnected && client && client->connected()) {
        // Проверяем живость соединения дополнительно (AT+CIPSTATUS)
        modem->sendAT("AT+CIPSTATUS");
        String statusResp;
        if (modem->waitResponse(1000, statusResp) == 1) {
            if (statusResp.indexOf("CONNECT OK") >= 0 ||
                statusResp.indexOf("STATE: CONNECT") >= 0) {
                return true;
            }
        }
        // Если проверка не удалась – закроем и пересоздадим
        httpEnd();
    }

    clearModemBuffer();

    if (!modem->isGprsConnected()) {
        Serial.println("[HTTP] GPRS not connected");
        return false;
    }

    Serial.printf("[HTTP] Connecting to %s:%d ...\n", host, port);
    if (!client->connect(host, port)) {
        Serial.println("[HTTP] TCP connection failed");
        return false;
    }
    // Даём модему время выдать CONNECT
    delay(1500);
    clearModemBuffer(); // убираем технические строки

    _httpHost = host;
    _httpPort = port;
    _httpConnected = true;
    Serial.println("[HTTP] Connected");
    return true;
}

// =====================================================================
// httpSendPacket() – Отправка одного POST-запроса (без повторных попыток)
// =====================================================================
/**
 * @brief Отправляет POST-запрос с бинарными данными на открытое TCP-соединение.
 * @param payload указатель на данные
 * @param length размер данных
 * @param deviceId строка идентификатора устройства (вставляется в заголовок X-Device-Id)
 * @param path строка пути (например, "/api/data")
 * @return true если запрос отправлен и получен ответ (код 200/202), иначе false.
 * @details Формирует HTTP/1.1 запрос с заголовками Host, Content-Type, Content-Length, Connection.
 *          Отправляет сначала заголовки, затем тело. Ожидает доступность ответа до 20 секунд.
 *          При успешной отправке вызывает readFullHttpResponse() для чтения ответа.
 *          В случае ошибки не переподключается – это делает httpSendPacketSafe().
 */
bool httpSendPacket(const uint8_t *payload, size_t length, const char *deviceId, const char *path) {
    if (!_httpConnected || !client->connected()) {
        Serial.println("[HTTP] Not connected, call httpBegin() first");
        return false;
    }

    clearModemBuffer();

    String request;
    request.reserve(256 + length);
    request = "POST " + String(path) + " HTTP/1.1\r\n";
    request += "Host: " + _httpHost + "\r\n";
    request += "Content-Type: application/octet-stream\r\n";
    request += "X-Device-Id: " + String(deviceId) + "\r\n";
    request += "Content-Length: " + String(length) + "\r\n";
    request += "Connection: keep-alive\r\n";
    request += "\r\n";

    // Отправляем заголовки
    size_t sent = client->print(request);
    if (sent != request.length()) {
        Serial.printf("[HTTP] Header send error: sent %u of %u\n", sent, request.length());
        return false;
    }
    // Отправляем тело
    size_t bodySent = client->write(payload, length);
    if (bodySent != length) {
        Serial.println("оправлен не весь пакет");
        return false;
    }
    // Даём немного времени на отправку, проверяя URC
    unsigned long timeout = millis() + 20000;
    while (!client->available() && millis() < timeout) {
        if (simSerial->available()) {
            String urc = simSerial->readStringUntil('\n');
            Serial.print("[URC] ");
            Serial.println(urc);
        }
        delay(10);
        yield();
    }

    if (!client->available()) {
        Serial.println("[HTTP] No response (timeout)");
        return false;
    }

    // Читаем ответ до конца
    int httpCode = -1;
    if (!readFullHttpResponse(httpCode)) {
        httpEnd();
        return false;
    }

    return (httpCode == 200 || httpCode == 202);
}

// =====================================================================
// httpSendPacketSafe() – Отправка POST с автоматическими повторами
// =====================================================================
/**
 * @brief Отправляет POST-запрос с автоматическим восстановлением соединения и повторными попытками.
 * @param payload указатель на данные
 * @param length размер данных
 * @param deviceId идентификатор устройства
 * @param path путь на сервере
 * @param host хост для подключения (используется при повторном установлении соединения)
 * @param port порт
 * @return true если отправка успешна (код 200/202), иначе false.
 * @details Делает до 2 попыток (maxRetries=2). На каждой итерации:
 *          - проверяет GPRS, при необходимости переподключает (но сейчас закомментировано)
 *          - если нет TCP, вызывает httpBegin()
 *          - отправляет через httpSendPacket()
 *          - при неудаче закрывает TCP и повторяет.
 *          Возвращает true только при получении успешного кода.
 */
bool httpSendPacketSafe(const uint8_t *payload, size_t length, const char *deviceId,
                        const char *path, const char *host, int port) {
    const int maxRetries = 2;
    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        // Проверяем здоровье сети и GPRS
        if (!modem || !isActive) return false;

        if (!modem->isGprsConnected()) {
            Serial.println("[HTTP] GPRS lost, reconnecting...");
            return false; // в данном варианте не переподключаем автоматически – возврат
            // if (!connect(apn, gprsUser, gprsPass)) { ... } // закомментировано
        }

        // Если нет активного TCP – открываем
        if (!_httpConnected || !client || !client->connected()) {
            if (!modem->isGprsConnected()) {
                Serial.println("[HTTP] GPRS lost and havent gprs, return");
                return false;
            }
            httpEnd();
            if (!httpBegin(host, port)) {
                Serial.println("[HTTP] TCP connect failed, retry...");
                delay(3000);
                continue;
            }
        }

        // Отправляем запрос
        if (!httpSendPacket(payload, length, deviceId, path)) {
            httpEnd(); // разрыв, будем пробовать заново
            delay(500);
            continue;
        }

        // Если дошло до сюда – ответ прочитан и код 200/202
        return true;
    }
    Serial.println("[HTTP] All attempts failed");
    return false;
}

// =====================================================================
// httpEnd() – Корректное закрытие TCP-соединения
// =====================================================================
void httpEnd() {
    if (client) {
        client->stop();
        delay(100);
        clearModemBuffer();
    }
    _httpConnected = false;
    _httpHost = "";
    _httpPort = 0;
    Serial.println("[HTTP] Connection closed");
}

} // namespace SimModule

#endif // NET > 0