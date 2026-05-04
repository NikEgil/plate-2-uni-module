// 🔹 1. INCLUDES — дефайны ДО заголовков
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_DEBUG Serial
#define MQTT_SOCKET_TIMEOUT 30 // Увеличенный таймаут сокета
#include <string.h>            // для strlen, memset
#include "sim.h"
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <TinyGsmClient.h> // ← TinyGsm определится только после #define выше
#include <esp_task_wdt.h>
#if NET > 0 
// 🔹 2. NAMESPACE — оставляем как есть (указатели)
namespace {
SoftwareSerial *simSerial = nullptr;
TinyGsm *modem = nullptr;
TinyGsmClient *client = nullptr;
PubSubClient *mqtt = nullptr;

bool isActive = false;
bool isGprsConnected = false;
int simRxPin = -1;
int simTxPin = -1;
uint32_t simBaud = 0;
} // namespace

namespace SimModule {

// 🔹 3. begin() — исправляем инициализацию объектов
void begin(int rxPin, int txPin, uint32_t baud) {
    if (rxPin == -1)
        rxPin = SIMRX;
    if (txPin == -1)
        txPin = SIMTX;
    if (baud == 0)
        baud = SIM_BAUD;

    Serial.println("\n=== [SIM] begin() START ===");
    Serial.printf("[SIM] Parameters: RX=%d, TX=%d, Baud=%lu\n", rxPin, txPin,
                  baud);

    // Очистка старого SoftwareSerial
    Serial.println("[SIM] Step 1: Deleting old SoftwareSerial if exists...");
    if (simSerial) {
        delete simSerial;
        simSerial = nullptr;
        Serial.println("[SIM] Old object deleted");
    }

    // Создание SoftwareSerial (ОДИН РАЗ)
    Serial.println("[SIM] Step 2: Creating SoftwareSerial with pins...");
    simSerial = new SoftwareSerial(rxPin, txPin);
    Serial.println("[SIM] SoftwareSerial created");

    Serial.println("[SIM] Step 3: Calling begin(baud)...");
    Serial.println("[SIM] Disabling WDT temporarily...");
    esp_task_wdt_deinit();

    simSerial->begin(baud); // ← Убрано дублирование new SoftwareSerial

    Serial.println("[SIM] Re-enabling WDT...");
    esp_task_wdt_init(5, true);
    Serial.println("[SIM] begin(baud) completed");

    Serial.println("[SIM] Step 4: Setting timeout...");
    simSerial->setTimeout(1000);
    Serial.println("[SIM] Timeout set");

    // 🔹 Инициализация модема: разыменовываем указатели через *
    Serial.println("[SIM] Step 5: Creating TinyGsm object...");
    modem = new TinyGsm(*simSerial); // ← *simSerial, не simSerial
    Serial.println("[SIM] TinyGsm created");

    Serial.println("[SIM] Step 6: Creating TinyGsmClient object...");
    client = new TinyGsmClient(*modem); // ← *modem
    Serial.println("[SIM] TinyGsmClient created");

    Serial.println("[SIM] Step 7: Creating PubSubClient object...");
    mqtt = new PubSubClient(*client);

    mqtt->setSocketTimeout(MQTT_SOCKET_TIMEOUT); // ← Увеличь таймаут
    mqtt->setKeepAlive(60);   // ← Уменьши keepalive (чаще пинг)
    mqtt->setBufferSize(512); // ← Увеличь буфер
    Serial.println("[SIM] MQTT configured");
    Serial.println("[SIM] MQTT created");

    Serial.println("=== [SIM] begin() SUCCESS ===\n");
}

void activate(bool act) {
    Serial.println("\n=== [SIM] activate() START ===");
    Serial.printf("[SIM] Requested state: %s\n", act ? "ON" : "OFF");

    if (act && !isActive) {
        Serial.println("[SIM] Step 1: Checking if serial is initialized...");
        if (!simSerial) {
            Serial.println(
                "[SIM] ERROR: simSerial is NULL! Call begin() first!");
            return;
        }
        Serial.println("[SIM] simSerial is OK");
        isActive = true;
        Serial.println("[SIM] activate() = TRUE");
        Serial.println("=== [SIM] activate() SUCCESS ===\n");
    } else if (!act && isActive) {
        Serial.println("[SIM] Step 1: Disconnecting GPRS...");
        if (isGprsConnected) {
            modem->gprsDisconnect();
            isGprsConnected = false;
            Serial.println("[SIM] GPRS disconnected");
        } else {
            Serial.println("[SIM] GPRS was not connected");
        }

        Serial.println("[SIM] Step 2: Closing serial port...");
        if (simSerial) {
            simSerial->end();
            Serial.println("[SIM] Serial port closed");
        }

        isActive = false;
        Serial.println("[SIM] activate() = FALSE");
        Serial.println("=== [SIM] activate() SUCCESS ===\n");
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

    // 🔹 Временно увеличиваем WDT таймаут
    esp_task_wdt_init(60, false); // 60 сек, отключен

    // Ждём сеть (3 попытки по 15 сек)
    Serial.println(F("Waiting for network..."));
    for (int i = 1; i <= 3; i++) {
        Serial.printf("Try %d/3... ", i);

        // 🔹 Разбиваем ожидание на части с yield()
        unsigned long start = millis();
        bool networkReady = false;
        while (millis() - start < 15000) {
            yield(); // 🔹 Сброс WDT
            if (modem->isNetworkConnected()) {
                networkReady = true;
                break;
            }
            delay(100); // 🔹 Даём время на обработку
        }

        if (networkReady) {
            Serial.println(F("OK"));
            break;
        }

        if (i == 3) {
            Serial.println(F("Failed"));
            esp_task_wdt_init(8, true); // Возвращаем WDT
            return false;
        }
        delay(200);
    }

    // Подключаемся к GPRS (3 попытки)
    Serial.println(F("Connecting to GPRS..."));
    for (int i = 1; i <= 3; i++) {
        Serial.printf("Try %d/3... ", i);

        yield(); // 🔹 Сброс WDT перед подключением

        if (modem->gprsConnect(apn, user, pass)) {
            Serial.println(F("OK"));
            isGprsConnected = true;
            break;
        }

        if (i == 3) {
            Serial.println(F("Failed"));
            esp_task_wdt_init(8, true); // Возвращаем WDT
            return false;
        }

        delay(200);
        yield(); // 🔹 Сброс WDT между попытками
    }

    // 🔹 Возвращаем стандартный WDT
    esp_task_wdt_init(8, true);
    yield();

    delay(200);

    // Инфо для отладки
    Serial.printf("Signal: %d\n", getSignalQuality());
    Serial.printf("IP: %s\n", getLocalIP().c_str());

    return true;
}
bool factoryReset() {
    if (!modem || !isActive) {
        Serial.println("❌ Modem not active or initialized");
        return false;
    }

    Serial.println("🔄 SIM800: Starting factory reset...");

    // 🔹 Отключаем панику WDT и увеличиваем таймаут (процесс займёт ~10-15 сек)
    esp_task_wdt_init(30, false);
    yield();

    bool success = true;

    // 1. Сброс к заводским настройкам
    Serial.println("  → AT&F (Factory defaults)...");
    // modem->sendAT("+&F");
    String ans="";
    if (modem->factoryDefault())
    {
        if (modem->waitResponse(3000,ans) != 1) {
            Serial.println("  ⚠️ AT&F timeout/failed");
            success = false;
        }
         Serial.println(ans);
    }
    // 2. Сохранение настроек в NVRAM
    // Serial.println("  → AT&W (Save to memory)...");
    // modem->sendAT("+&W");
    // if (modem->waitResponse(3000) != 1) {
    //     Serial.println("  ️ AT&W timeout/failed");
    //     success = false;
    // }

    delay(500); // Ждём завершения записи в энергонезависимую память
    // 3. Перезагрузка модуля
    Serial.println("  → AT+CFUN=1,1 (Reboot)...");
    modem->sendAT(GF("+CFUN=1,1"));
    // Команда не возвращает OK, модуль уходит в аппаратный ребут

    // 4. Ожидание готовности после ребута
    Serial.println("  → Waiting for reboot completion...");
    unsigned long start = millis();
    bool isReady = false;
    while (millis() - start < 15000) {
        yield(); // 🔹 Критично: сброс WDT ESP32-S2 во время ожидания
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

    // 🔹 Восстанавливаем стандартный WDT
    esp_task_wdt_init(8, true);
    yield();

    // Сбрасываем флаги состояния (GPRS теряется при ребуте)
    isGprsConnected = false;
    // isActive остаётся true, так как UART не закрывался

    return success;
}
void disconnect() {
    if (!isGprsConnected) {
        Serial.println("⚠️ GPRS already disconnected");
        return;
    }

    Serial.println("📡 SimModule: Disconnecting GPRS...");
    // while (simSerial->available()) {
    //     simSerial->read();
    // }
    // // 🔹 1. Отключаем GPRS с проверкой статуса
    // bool result = modem->gprsDisconnect();

    // // 🔹 2. Ждём подтверждения от модема (критично!)
    // delay(500);
    // // while (simSerial->available()) {
    //     simSerial->read();
    // }
    // 🔹 3. Очищаем UART-буфер от "хвостов"


    // 🔹 4. Проверяем реальное состояние
    if (modem->isGprsConnected()) {
        Serial.println("⚠️ GPRS disconnect forcing...");
        // Принудительный сброс соединения
        modem->sendAT("+CIPSHUT");
        modem->waitResponse(2000);
        delay(300);
    }

    // 🔹 5. Только теперь обновляем флаги
    isGprsConnected = false;
    Serial.println("✅ GPRS disconnected");

    // 🔹 6. isActive НЕ сбрасываем здесь!
    // Модуль остаётся инициализированным для повторного подключения
    // isActive = false;  // ← УБРАТЬ отсюда
}

int ccid(byte *ccid) {

    String iccid = modem->getSimCCID();
    Serial.println(iccid);

    size_t len = iccid.length();
    memset(ccid, 0, 10);
    for (size_t i = 0; i < 10; i++) {
        uint8_t high = 0, low = 0;
        // Берём символы, если вышли за длину — добиваем '0'
        char c1 = (i * 2 < len) ? iccid[i * 2] : '0';
        char c2 = (i * 2 + 1 < len) ? iccid[i * 2 + 1] : '0';

        if (c1 >= '0' && c1 <= '9')
            high = c1 - '0';
        if (c2 >= '0' && c2 <= '9')
            low = c2 - '0';

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

    // waitResponse возвращает: 1 = OK, 2 = ERROR, 0 = Timeout
    int8_t status = modem->waitResponse(3000, outResponse);

    if (status == 1) {
        Serial.println("✅ OK");
        Serial.println(outResponse);
        return true;
    } else if (status == 2) {
        Serial.println("❌ ERROR from modem");
    } else {
        Serial.println("⏱️ Timeout waiting for response");
    }

    // Отладочный вывод сырого ответа (полезно для парсинга)
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

    // Включаем авто-обновление времени от сети
    modem->sendAT(GF("+CLTS=1"));
    if (modem->waitResponse(1000) != 1) {
        Serial.println("[SIM] Failed to set AT+CLTS=1");
        return false;
    }

    // Сохраняем в память модема
    modem->sendAT(GF("&W"));
    modem->waitResponse(1000);

    Serial.println("[SIM] Time sync enabled (AT+CLTS=1 + AT&W)");
    return true;
}

NetTime getNetworkTime() {
    NetTime result = {0};
    if (!isActive || !modem)
        return result;

    modem->sendAT(GF("+CCLK?"));
    String res;
    if (modem->waitResponse(3000, res) != 1)
        return result;

    // Извлекаем только то, что внутри кавычек
    int start = res.indexOf('"');
    int end = res.indexOf('"', start + 1);
    if (start < 0 || end < 0)
        return result;

    String timeStr = res.substring(start + 1, end);

    // Парсим: YY/MM/DD,HH:MM:SS+TZ(quarters)
    if (sscanf(timeStr.c_str(), "%d/%d/%d,%d:%d:%d+%d", &result.year,
               &result.month, &result.day, &result.hour, &result.minute,
               &result.second, &result.timezone) != 7) {
        return result;
    }

    if (result.year < 100)
        result.year += 2000;
    result.valid = true;
    return result;
}

bool syncSystemClock() {
    for (int attempt = 1; attempt <= 3; attempt++) {
        NetTime nt = getNetworkTime();

        // Отсекаем заводской fallback (2004) и невалидные данные
        if (!nt.valid || nt.year < 2020) {
            Serial.printf("[SIM] Invalid time (year %d), retry %d/3...\n",
                          nt.year, attempt);
            delay(5000);
            continue;
        }

        // Конвертируем в struct tm
        struct tm t = {0};
        t.tm_year = nt.year - 1900;
        t.tm_mon = nt.month - 1;
        t.tm_mday = nt.day;
        t.tm_hour = nt.hour;
        t.tm_min = nt.minute;
        t.tm_sec = nt.second;
        t.tm_isdst = -1;

        time_t ts = mktime(&t);

        // ⚠️ NITZ отдаёт локальное время. Для UTC вычитаем таймзону:
        // ts -= nt.timezone * 15 * 60;

        struct timeval tv = {.tv_sec = ts, .tv_usec = 0};
        settimeofday(&tv, nullptr);

        Serial.printf("[SIM] ✓ Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                      nt.year, nt.month, nt.day, nt.hour, nt.minute, nt.second);
        return true;
    }

    Serial.println("[SIM] ✗ Failed to sync after 3 attempts");
    return false;
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

    time_t ts = mktime(&timeinfo);
    // Если нужен UTC: ts -= nt.timezone * 900;
    return ts;
}

void buildTopic(char *outTopic, size_t size) {
    snprintf(outTopic, size, "mqtt/devices/%s/data", IDchar);
}

bool mqttConnect() {
    // 🔹 1. Проверка инициализации
    if (!mqtt || !client || !modem) {
        Serial.println("❌ MQTT components not initialized");
        return false;
    }

    // Уже подключены — выходим
    if (mqtt->connected()) {
        Serial.println("✅ Already connected");
        return true;
    }

    // 🔹 2. Проверка GPRS перед MQTT
    if (!modem->isGprsConnected()) {
        Serial.println("⚠️ GPRS not connected, skipping MQTT");
        return false;
    }

    // 🔹 3. Перенастройка клиента (без пересоздания!)
    mqtt->setServer(broker, 1883);
    mqtt->setSocketTimeout(15); // Таймаут сокета
    mqtt->setKeepAlive(60);     // Пинг каждые 60 сек
    mqtt->setBufferSize(512);   // Буфер под пакет

    // 🔹 4. Чистый сокет перед подключением
    if (client->connected()) {
        Serial.println("Closing stale socket...");
        client->stop();
        delay(100);
        yield(); // Сброс аппаратного WDT
    }

    // 🔹 5. Отладочная информация
    Serial.printf("Signal: %d%%, IP: %s\n", getSignalQuality(),
                  modem->getLocalIP().c_str());
    Serial.print("Connecting MQTT...");
    Serial.flush(); // Гарантируем вывод до блокировки

    // 🔹 6. Отключаем Task WDT на время подключения
    esp_task_wdt_init(60, false); // 60 сек, выключен
    delay(10);

    // 🔹 7. ОДНА попытка подключения (не цикл!)
    Serial.print("[attempting]");
    Serial.flush();

    unsigned long start = millis();
    bool status = mqtt->connect(IDchar, IDchar, pass);
    unsigned long elapsed = millis() - start;

    Serial.printf("[done in %lu ms] ", elapsed);

    // 🔹 8. Включаем WDT обратно СРАЗУ после connect()
    esp_task_wdt_init(8, true);
    yield();

    // 🔹 9. Успех
    if (status) {
        Serial.println("✅ SUCCESS");
        mqtt->loop(); // Обработка входящих
        return true;
    }

    // 🔹 10. Диагностика ошибки
    int rc = mqtt->state();
    Serial.printf("FAILED (rc=%d)\n", rc);

    switch (rc) {
    case -4:
        Serial.println("  → Connection refused (check credentials)");
        break;
    case -3:
        Serial.println("  → Connection lost (network unstable)");
        break;
    case -2:
        Serial.println("  → Connection lost during handshake");
        break;
    case -1:
        Serial.println("  → Connection failed (broker unreachable)");
        break;
    case 1:
        Serial.println("  → Connected but protocol error");
        break;
    case 2:
        Serial.println("  → Invalid client ID");
        break;
    case 3:
        Serial.println("  → Server unavailable");
        break;
    case 4:
        Serial.println("  → Bad username/password");
        break;
    case 5:
        Serial.println("  → Not authorized");
        break;
    default:
        Serial.printf("  → Unknown error: %d\n", rc);
        break;
    }

    // 🔹 11. Восстановление состояния клиента (без пересоздания!)
    // При ошибках -1/-2/-3 сбрасываем сокет и параметры
    if (rc == -1 || rc == -2 || rc == -3) {
        Serial.println("  → Resetting client state...");

        // Закрываем сокет
        if (client->connected()) {
            client->stop();
            delay(100);
        }

        // Перенастраиваем (те же параметры, что в begin)
        mqtt->setServer(broker, 1883);
        mqtt->setSocketTimeout(15);
        mqtt->setKeepAlive(60);
        mqtt->setBufferSize(512);

        Serial.println("  → Client state reset");
        delay(200);
        yield();
    }

    // 🔹 12. Проверка памяти после попытки
    Serial.printf("Heap after: %d bytes\n", ESP.getFreeHeap());

    return false;
}

bool mqttSendPacket(const uint8_t *payload, size_t length) {
    if (!mqtt || !mqtt->connected()) { // ← mqtt->
        Serial.println("❌ MQTT not connected");
        return false;
    }

    char topic[64];
    buildTopic(topic, sizeof(topic));
    Serial.printf("📡 Sending %d bytes to %s ... ", length, topic);

    bool result = mqtt->publish(topic, payload, length); // ← mqtt->
    mqtt->loop();                                        // ← mqtt->

    Serial.println(result ? "OK" : "FAIL");
    return result;
}

void mqttdisconnect() {              // ← исправил опечатку в имени
    if (mqtt && mqtt->connected()) { // ← mqtt->
        mqtt->disconnect();          // ← mqtt->
        Serial.println("mqtt disconected");
    }
}
} // namespace SimModule
#endif