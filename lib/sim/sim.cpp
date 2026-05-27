#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_DEBUG Serial
#define MQTT_SOCKET_TIMEOUT 30
#include "sim.h"
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <TinyGsmClient.h>
#include <defenitions.h>
#include <esp_task_wdt.h>
#include <string.h>
#if NET > 0

namespace {
HardwareSerial *simSerial = nullptr; // используем UART1
TinyGsm *modem = nullptr;
TinyGsmClient *client = nullptr;
PubSubClient *mqtt = nullptr;
bool isActive = false;
bool isGprsConnected = false;

} // namespace

namespace SimModule {
void begin(int rxPin, int txPin, uint32_t baud) {
    // 1. Принудительная очистка предыдущего состояния
    end();
    // 2. Параметры по умолчанию
    if (rxPin == -1)
        rxPin = SIMRX;
    if (txPin == -1)
        txPin = SIMTX;
    if (baud == 0)
        baud = SIM_BAUD;
    Serial.println("\n=== [SIM] begin() (HardwareSerial) ===");
    Serial.printf("[SIM] Parameters: RX=%d, TX=%d, Baud=%lu\n", rxPin, txPin,
                  baud);
    // 3. Инициализируем аппаратный UART (Serial1)
    simSerial = &Serial1;
    simSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
    simSerial->setTimeout(1000);
    // 4. Создаём объекты TinyGSM
    modem = new TinyGsm(*simSerial);
    client = new TinyGsmClient(*modem);
    modem->sendAT(GF("E0")); // выключаем эхо
    modem->waitResponse(500);
    modem->sendAT(GF("+CMGF=1")); // текстовый режим SMS (обычно не нужно)
    modem->waitResponse(500);
    modem->sendAT(GF("+CNMI=0,0,0,0,0")); // отключаем индикацию новых сообщений
    modem->waitResponse(500);
    modem->sendAT(GF("+CREG=0")); // отключаем URC о регистрации в сети
    modem->waitResponse(500);
    modem->sendAT(GF("+CGREG=0")); // отключаем URC о GPRS-регистрации
    modem->waitResponse(500);
    modem->sendAT(GF("+CSCLK=0")); // отключаем slow clock (может влиять)
    modem->waitResponse(500);
    modem->sendAT(GF("+CIPTIMEOUT=20"));
    modem->waitResponse(1000);
    modem->sendAT(GF("&W"));
    modem->waitResponse(1000);
    mqtt = new PubSubClient(*client);
    mqtt->setSocketTimeout(MQTT_SOCKET_TIMEOUT);
    mqtt->setKeepAlive(60);
    mqtt->setBufferSize(512);
    Serial.println("=== [SIM] begin() SUCCESS ===");
}
// void begin(int rxPin, int txPin, uint32_t baud) {
//     // 1. Принудительная очистка, если пользователь забыл вызвать end()
//     end();
//     // 2. Параметры по умолчанию
//     if (rxPin == -1)
//         rxPin = SIMRX;
//     if (txPin == -1)
//         txPin = SIMTX;
//     if (baud == 0)
//         baud = SIM_BAUD;
//     Serial.println("\n=== [SIM] begin() START ===");
//     Serial.printf("[SIM] Parameters: RX=%d, TX=%d, Baud=%lu\n", rxPin, txPin,
//                   baud);
//     // 3. Создание объектов
//     simSerial = new SoftwareSerial(rxPin, txPin);
//     // Временно увеличиваем таймаут WDT на время настройки
//     esp_task_wdt_init(10, false);
//     simSerial->begin(baud);
//     esp_task_wdt_init(8, true); // возврат к стандартным 8 сек
//     simSerial->setTimeout(1000);
//     modem = new TinyGsm(*simSerial);
//     client = new TinyGsmClient(*modem);
//     // Устанавливаем таймаут TCP-соединения 20 секунд и сохраняем
//     modem->sendAT(GF("+CIPTIMEOUT=20"));
//     modem->waitResponse(1000);
//     modem->sendAT(GF("&W"));
//     modem->waitResponse(1000);
//     mqtt = new PubSubClient(*client);
//     mqtt->setSocketTimeout(MQTT_SOCKET_TIMEOUT);
//     mqtt->setKeepAlive(60);
//     mqtt->setBufferSize(512);
//     Serial.println("=== [SIM] begin() SUCCESS ===");
// }

//-----------------------------------------------------
// end() – освобождение ресурсов
//-----------------------------------------------------
// void end() {
//     Serial.println("[SIM] end() – releasing resources");
//     if (mqtt) {
//         mqtt->disconnect();
//         delete mqtt;
//         mqtt = nullptr;
//     }
//     if (client) {
//         delete client;
//         client = nullptr;
//     }
//     if (modem) {
//         delete modem;
//         modem = nullptr;
//     }
//     if (simSerial) {
//         simSerial->end();
//         delete simSerial;
//         simSerial = nullptr;
//     }
//     isActive = false;
//     isGprsConnected = false;
// }
void end() {
    Serial.println("[SIM] end() – releasing resources");
    if (mqtt) {
        mqtt->disconnect();
        delete mqtt;
        mqtt = nullptr;
    }
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

void activate(bool act) {
    Serial.println("\n=== [SIM] activate() START ===");
    Serial.printf("[SIM] Requested state: %s\n", act ? "ON" : "OFF");
    if (act && !isActive) {
        if (!simSerial) {
            Serial.println(
                "[SIM] ERROR: simSerial is NULL! Call begin() first!");
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
        Serial.printf("[SIM] Already in requested state: isActive=%d\n",
                      isActive);
    }
    delay(100);
}

bool connect(const char *apn, const char *user, const char *pass) {
    if (!isActive) {
        Serial.println("SIM not activated");
        return false;
    }
    Serial.print(F("Modem: "));
    Serial.println(modem->getModemName());
    esp_task_wdt_init(120, false);
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
            esp_task_wdt_init(8, true);
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

bool AT(const String &cmd, String &outResponse) {
    if (!modem) {
        Serial.println("❌ Modem not initialized");
        return false;
    }
    Serial.printf("📡 AT: %s\n", cmd.c_str());
    modem->sendAT(cmd);
    int8_t status = modem->waitResponse(3000, outResponse);
    Serial.printf("DEBUG AT: cmd=%s, status=%d, raw='%s'\n", cmd.c_str(),
                  status, outResponse.c_str());
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

bool isConnection() { return isGprsConnected && isActive; }
bool hasNetwork() { return isActive && modem->isNetworkConnected(); }

int getSignalQuality() {
    return isActive ? modem->getSignalQuality() * 100 / 31 : -1;
}

String getLocalIP() { return isActive ? modem->getLocalIP() : ""; }
String getModemInfo() { return isActive ? modem->getModemName() : ""; }
void *getClient() { return static_cast<void *>(client); }

bool enableTimeSync() {
    if (!isActive || !modem)
        return false;
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
NetTime getNetworkTime() {
    NetTime result = {0};
    result.valid = false;

    if (!isActive || !modem)
        return result;

    // Очистить буфер модема перед командой
    // modem->waitResponse(0);  // вычитать все накопившиеся данные

    modem->sendAT(GF("+CCLK?"));
    String res;
    int8_t status = modem->waitResponse(5000, res); // увеличенный таймаут 5 с
    if (status != 1) {
        Serial.printf("[SIM] getNetworkTime: no response (status=%d)\n",
                      status);
        return result;
    }

    // Ищем подстроку "+CCLK: "
    int cclkPos = res.indexOf("+CCLK: ");
    if (cclkPos < 0) {
        Serial.println("[SIM] getNetworkTime: '+CCLK: ' not found");
        return result;
    }

    // Находим кавычки вокруг времени
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
    // timeStr имеет формат "YY/MM/DD,HH:MM:SS+TZ" (например,
    // "25/05/14,12:30:45+12")

    int year, month, day, hour, minute, second, tz;
    char tzSign;
    int fields = sscanf(timeStr.c_str(), "%d/%d/%d,%d:%d:%d%c%d", &year, &month,
                        &day, &hour, &minute, &second, &tzSign, &tz);

    if (fields < 8) {
        Serial.printf(
            "[SIM] getNetworkTime: sscanf failed (got %d fields) from '%s'\n",
            fields, timeStr.c_str());
        return result;
    }

    if (tzSign == '-')
        tz = -tz;
    if (year < 100)
        year += 2000;

    // Проверка разумности значений
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 ||
        hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        Serial.printf("[SIM] getNetworkTime: invalid date/time: %04d-%02d-%02d "
                      "%02d:%02d:%02d\n",
                      year, month, day, hour, minute, second);
        return result;
    }

    result.year = year;
    result.month = month;
    result.day = day;
    result.hour = hour;
    result.minute = minute;
    result.second = second;
    result.timezone = tz; // в четвертях часа
    result.valid = true;
    return result;
}

int syncSystemClock() {
    // Защита от слишком частых вызовов
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
        Serial.printf("[SIM] Invalid time: %04d-%02d-%02d\n", nt.year, nt.month,
                      nt.day);
        return 0;
    }
    struct tm t = {0};
    t.tm_year = nt.year - 1900;
    t.tm_mon = nt.month - 1;
    t.tm_mday = nt.day;
    t.tm_hour = nt.hour;
    t.tm_min = nt.minute;
    t.tm_sec = nt.second;
    t.tm_isdst = -1;
    time_t ts = mktime(&t);
    if (ts < 0) {
        Serial.println("[SIM] mktime() failed");
        return 0;
    }
    struct timeval tv = {.tv_sec = ts, .tv_usec = 0};
    if (settimeofday(&tv, nullptr) != 0) {
        Serial.println("[SIM] settimeofday() failed");
        return 0;
    }
    Serial.printf("[SIM] ✓ Synced: %04d-%02d-%02d %02d:%02d:%02d\n", nt.year,
                  nt.month, nt.day, nt.hour, nt.minute, nt.second);
    return 1;
}

time_t getTimestamp() {
    NetTime nt = getNetworkTime();
    if (!nt.valid)
        return 0;
    struct tm timeinfo = {0};
    timeinfo.tm_year = nt.year - 1900;
    timeinfo.tm_mon = nt.month - 1;
    timeinfo.tm_mday = nt.day;
    timeinfo.tm_hour = nt.hour;
    timeinfo.tm_min = nt.minute;
    timeinfo.tm_sec = nt.second;
    timeinfo.tm_isdst = -1;
    return mktime(&timeinfo);
}

void buildTopic(char *outTopic, size_t size) {
    snprintf(outTopic, size, "mqtt/devices/%s/data", IDchar);
}
bool mqttConnect(String broker, int port, String pass) {
    Serial.println("        try mqtt connect");
    if (!mqtt || !client || !modem) {
        Serial.println("❌ MQTT components not initialized");
        return false;
    }
    if (mqtt->connected()) {
        Serial.println("✅ Already connected");
        return true;
    }
    if (!modem->isGprsConnected()) {
        Serial.println("⚠️ GPRS not connected, skipping MQTT");
        return false;
    }
    String ip;
    for (int i = 0; i < 10; i++) {
        ip = modem->getLocalIP();
        if (ip.length() > 0)
            break;
        delay(500);
        yield();
    }
    if (ip.length() == 0) {
        Serial.println("No IP address");
        return false;
    }
    Serial.printf("Signal: %d%%, IP: %s\n", getSignalQuality(), ip.c_str());
    if (client->connected()) {
        client->stop();
        delay(100);
    }
    modem->sendAT(GF("+CIPTIMEOUT=10"));
    modem->waitResponse(500);
    Serial.print("Opening TCP connection... ");
    unsigned long tcpStart = millis();
    Serial.println(broker.c_str());
    Serial.println(port);
    Serial.println(IDchar);
    Serial.println(pass);
    bool tcpOk = client->connect(broker.c_str(), port, 10000); // 10 секунд
    unsigned long tcpElapsed = millis() - tcpStart;
    Serial.printf("[%lu ms] %s\n", tcpElapsed, tcpOk ? "OK" : "FAIL");
    if (!tcpOk) {
        Serial.println("TCP connection failed, aborting MQTT");
        client->stop(); // на всякий случай
        return false;
    }
    mqtt->setServer(broker.c_str(), 1883);
    mqtt->setSocketTimeout(10); // таймаут на пакеты MQTT
    mqtt->setKeepAlive(60);
    mqtt->setBufferSize(512);
    Serial.printf("Signal: %d%%, IP: %s\n", getSignalQuality(), ip.c_str());
    Serial.print("Starting MQTT handshake... ");
    unsigned long mqttStart = millis();
    bool status = mqtt->connect(IDchar, IDchar, pass.c_str());
    unsigned long mqttElapsed = millis() - mqttStart;
    Serial.printf("[%lu ms] %s\n", mqttElapsed, status ? "SUCCESS" : "FAILED");
    if (!status) {
        int rc = mqtt->state();
        Serial.printf("MQTT error: rc=%d\n", rc);
        client->stop();
        return false;
    }
    mqtt->loop();
    return true;
}

bool mqttSendPacket(const uint8_t *payload, size_t length) {
    if (!mqtt) {
        Serial.println("❌ MQTT object not initialized");
        return false;
    }
    if (!mqtt->connected()) {
        Serial.println("MQTT not connected, trying to reconnect...");
        // if (!mqttConnect()) {
        //     Serial.println("Reconnection failed");
        //     return false;
        // }
    }
    char topic[64];
    buildTopic(topic, sizeof(topic));
    Serial.printf("📡 Sending %d bytes to %s ... ", length, topic);
    bool result = mqtt->publish(topic, payload, length);
    mqtt->loop();
    if (!result) {
        Serial.println("FAIL (publish failed)");
        //     client->stop();
        //     delay(100);
        //     // if (mqttConnect()) {
        //         result = mqtt->publish(topic, payload, length);
        //         mqtt->loop();
        //     }
    }
    Serial.println(result ? "OK" : "FAIL");
    return result;
}

void mqttDisconnect() {
    if (mqtt) {
        if (mqtt->connected()) {
            mqtt->disconnect();
        }
    }
    if (client) {
        client->stop(); // гарантированно рвём TCP
    }
    Serial.println("MQTT disconnected");
}
// -----------------------------------------------------
// Получить текст первого непрочитанного SMS (без удаления)
// -----------------------------------------------------
String getUnreadSMS() {
    if (!modem || !isActive)
        return "";

    // // 1. Временный текстовый режим
    // modem->sendAT(GF("+CMGF=1"));
    // if (modem->waitResponse(1000) != 1) {
    //     modem->sendAT(GF("+CMGF=0"));
    //     modem->waitResponse(500);
    //     return "";
    // }

    // 2. Запросить список непрочитанных
    modem->sendAT(GF("+CMGL=\"REC UNREAD\""));
    String response;
    if (modem->waitResponse(5000, response) != 1) {
        modem->sendAT(GF("+CMGF=0"));
        modem->waitResponse(500);
        return "";
    }

    // 3. Найти первый блок +CMGL: ...
    int pos = response.indexOf("+CMGL:");
    if (pos == -1) {
        //     modem->sendAT(GF("+CMGF=0"));
        //     modem->waitResponse(500);
        return ""; // нет непрочитанных
    }

    // Индекс SMS (не обязателен для возврата, но пригодится при удалении)
    int idx1 = response.indexOf(':', pos);
    int idx2 = response.indexOf(',', idx1);
    int smsIndex = (idx1 != -1 && idx2 != -1)
                       ? response.substring(idx1 + 1, idx2).toInt()
                       : -1;

    // 4. Текст сообщения – начинается со следующей строки после заголовка
    int lineEnd = response.indexOf('\n', pos);
    if (lineEnd == -1)
        lineEnd = response.length();
    String text;
    int textPos = lineEnd + 1;
    while (textPos < response.length()) {
        int nextLineEnd = response.indexOf('\n', textPos);
        if (nextLineEnd == -1)
            nextLineEnd = response.length();
        String nextLine = response.substring(textPos, nextLineEnd);
        nextLine.trim();
        if (nextLine.startsWith("+CMGL:") || nextLine == "OK")
            break;
        if (text.length() > 0)
            text += "\n";
        text += nextLine;
        textPos = nextLineEnd + 1;
    }

    // 5. Вернуть PDU-режим обратно
    // modem->sendAT(GF("+CMGF=0"));
    // modem->waitResponse(500);

    // (Опционально) удалить прочитанное SMS
    modem->sendAT("+CMGD=" + String(smsIndex));
    modem->waitResponse(1000);

    return text;
}
// Удаление одного SMS по индексу (тоже временно переключает режим)
bool deleteSMS(int index) {
    if (!modem)
        return false;
    // modem->sendAT(GF("+CMGF=1"));
    // modem->waitResponse(500);
    String cmd = "+CMGD=" + String(index);
    modem->sendAT(cmd);
    int8_t res = modem->waitResponse(1000);
    // modem->sendAT(GF("+CMGF=0"));
    // modem->waitResponse(500);
    return (res == 1);
}

} // namespace SimModule
#endif