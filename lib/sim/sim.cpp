#include "sim.h"
// TinyGSM требует определения модема ДО включения заголовка
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_DEBUG Serial // Отладка через USB-Serial
#include <TinyGsmClient.h>
#include <esp_task_wdt.h>
namespace {
SoftwareSerial *simSerial = nullptr;
TinyGsm *modem = nullptr;
TinyGsmClient *client = nullptr;

bool isActive = false;
bool isGprsConnected = false;

int simRxPin = -1;
int simTxPin = -1;
uint32_t simBaud = 0;
} // namespace

namespace SimModule {
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

    // Сохраняем параметры

    Serial.println("[SIM] Step 1: Deleting old SoftwareSerial if exists...");
    if (simSerial) {
        delete simSerial;
        simSerial = nullptr;
        Serial.println("[SIM] Old object deleted");
    }

    Serial.println("[SIM] Step 2: Creating SoftwareSerial with pins...");
    // Передаём пины в конструктор
    simSerial = new SoftwareSerial(rxPin, txPin);
    Serial.println("[SIM] SoftwareSerial created");

    Serial.println("[SIM] Step 3: Calling begin(baud)...");
    // begin() вызываем только с baud
    Serial.println("[SIM] Disabling WDT temporarily...");
    esp_task_wdt_deinit(); // Отключаем сторожевик

    simSerial = new SoftwareSerial(rxPin, txPin);
    simSerial->begin(baud);

    Serial.println("[SIM] Re-enabling WDT...");
    // Включаем обратно (таймаут 5 сек)
    esp_task_wdt_init(5, true);
    Serial.println("[SIM] begin(baud) completed");

    Serial.println("[SIM] Step 4: Setting timeout...");
    simSerial->setTimeout(1000);
    Serial.println("[SIM] Timeout set");

    Serial.println("[SIM] Step 5: Creating TinyGsm object...");
    if (!modem) {
        modem = new TinyGsm(*simSerial);
        Serial.println("[SIM] TinyGsm created");
    }

    Serial.println("[SIM] Step 6: Creating TinyGsmClient object...");
    if (!client) {
        client = new TinyGsmClient(*modem);
        Serial.println("[SIM] TinyGsmClient created");
    }

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

        // Serial.println("[SIM] Step 2: Restarting modem...");
        // Serial.println("[SIM] Sending AT command to restart...");

        // // Проверяем связь с модемом
        // Serial.println("[SIM] Step 3: Testing modem connection...");
        // bool modemExists = modem->testAT(2000);
        // Serial.printf("[SIM] Modem testAT result: %s\n", modemExists ? "OK" :
        // "FAILED");

        // if (!modemExists)
        // {
        //     Serial.println("[SIM] WARNING: Modem not responding, trying
        //     restart..."); modem->restart(); Serial.println("[SIM] Restart
        //     command sent");
        // }

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

    // Ждём сеть (3 попытки по 30 сек)
    Serial.println(F("Waiting for network..."));
    for (int i = 1; i <= 3; i++) {
        Serial.printf("Try %d/3... ", i);
        if (modem->waitForNetwork(30000L)) {
            Serial.println(F("OK"));
            break;
        }
        if (i == 3) {
            Serial.println(F("Failed"));
            return false;
        }
        delay(200);
    }

    // Подключаемся к GPRS (3 попытки)
    Serial.println(F("Connecting to GPRS..."));
    for (int i = 1; i <= 3; i++) {
        Serial.printf("Try %d/3... ", i);
        if (modem->gprsConnect(apn, user, pass)) {
            Serial.println(F("OK"));
            isGprsConnected = true;
            break;
        }
        if (i == 3) {
            Serial.println(F("Failed"));
            return false;
        }
        delay(200);
    }

    // Инфо для отладки
    Serial.printf("Signal: %d\n", getSignalQuality() * 100 / 31);
    Serial.printf("IP: %s\n", getLocalIP().c_str());

    return true;
}

void disconnect() {
    if (isGprsConnected) {
        modem->gprsDisconnect();
        isGprsConnected = false;
    }
}

bool isConnection() { return isGprsConnected && isActive; }
bool hasNetwork() { return isActive && modem->isNetworkConnected(); }

int getSignalQuality() { return isActive ? modem->getSignalQuality() : -1; }

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

} // namespace SimModule