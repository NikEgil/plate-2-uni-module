#include "FlashStack.h"
#include <Arduino.h>
#include <Preferences.h>
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
#if BOARD_TYPE == 2

// ===== ПЕРЕХВАТ SERIAL =====
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

// ===== 3. Создаём экземпляр =====
DualSerial Dual;

// ===== 4. ТОЛЬКО ТЕПЕРЬ подменяем Serial =====
#undef Serial
#define Serial Dual

// ===== ASYNC WEB SERVER =====
AsyncWebServer server(80);

const char *ssidwifi = "S2-Debug";
const char *passwifi = "12345678";

#endif

FlashStack stack;
uint8_t g_packet[198];
uint8_t a_packet[250];
static uint8_t rxBuffer[250];
Preferences prefs;
int Battery = 146;
byte tableSens[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
byte ccid[10] = {};
int signalp = 0;

String Pass = "";
String Broker = "";
uint16_t Port = 0;

void loadConfigFromNVS() {
    Preferences prefs;
    prefs.begin("sim-config", true); // read-only
    if (prefs.isKey("broker")) {
        Pass = prefs.getString("pass", "");
        Broker = prefs.getString("broker", "");
        Port = prefs.getString("port", "1883").toInt();
        Serial.println("[NVS] Загружены настройки:");
        Serial.printf("  Pass:   %s\n", Pass.c_str());
        Serial.printf("  Broker: %s\n", Broker.c_str());
        Serial.printf("  Port:   %d\n", Port);
    } else {
        Serial.println("[NVS] Нет сохранённых настроек");
        Pass = "";
        Broker = "";
        Port = 1883;
    }
    prefs.end();
}

void saveConfigFromNVS(String spass, String sbroker, int sport) {
    Preferences prefs;
    prefs.begin("sim-config", false);
    prefs.putString("pass", spass);
    prefs.putString("broker", sbroker);
    prefs.putString("port", String(sport));
    prefs.end();
    loadConfigFromNVS();
}

void cleanUpStack() {
    Serial.println(F("🧹 Cleaning stack..."));
    if (stack.clear()) {
        Serial.println(F("✅ Stack cleared"));
    }
}

#if BOARD_TYPE == 0
bool searche_multisens() {
    Serial.println("founding multisens");
    byte req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
    Serial.println("	request:");
    printHEX(req, sizeof(req));
    RsModbus::sendData(req, sizeof(req));

    delay(500);

    byte response[32] = {0};
    // Ждём ответ 150 мс (для Modbus обычно хватает 50-100 мс)
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
    // Ждём ответ 150 мс (для Modbus обычно хватает 50-100 мс)
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
    // enable_power(0);
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

    // Формируем запрос...
    byte req[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
    req[0] = (byte)sens;
    req[5] = (byte)lenreg;

    byte request[8];
    addCRC(req, 6, request);
    RsModbus::sendData(request, 8);
    delay(250);
    // Приём: передаём внешний буфер и его размер
    int received = RsModbus::receiveData(outBuf, outBufSize, 150);
    if (received > 0) {
        // Serial.printf("  RX[%d]: ", received - 1);
        printHEX(outBuf, received);
    }
    return received; // -1 = ошибка, >=0 = кол-во байт
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
        if (outBuf[0] == 0x24 and check_wh65lp_crc(outBuf, received)) {
            return received;
            // printHEX(outBuf, received);
        } else {
            delay(250);
        }
    }
    received = -1;
    return received;
    // -1 = ошибка, >=0 = кол-во байт
}

struct MeasureResult {
    uint8_t *data; // Указатель на данные
    size_t length; // Длина в байтах
    bool valid;    // Флаг успеха (память выделена + опрос прошёл)
};

#if BOARD_REV == 3

MeasureResult measure(int port) {
    MeasureResult res = {nullptr, 0, false};
    digitalWrite(LED_PIN, HIGH);
    enable_power(true);
    Serial.printf("Measure, port %i\n", port);
    enable_sens(port);
    delay(1000);
    // Выбор канала
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
#endif
#if BOARD_TYPE == 2
MeasureResult measure(int port) {
    MeasureResult res = {nullptr, 0, false};
    digitalWrite(LED_PIN, HIGH);
    enable_power(true);
    // Serial.printf("Measure, port %i\n", port);
    enable_sens(port);
    delay(2000);
    // Выбор канала
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
        res.length = 5 + lenreg * 2 + 1;
        res.data = (uint8_t *)malloc(res.length);
        received = pollingMeteo(sens, res.length, tempBuf, res.length);
        if (received == (int)res.length) {
            res.data[0] = (byte)port;
            memcpy(res.data + 1, tempBuf, received);
            printHEX(res.data, received);
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
    enable_power(0);
    if (res.valid) {
        Serial.println("✅ Measured");
    }
    digitalWrite(LED_PIN, LOW);
    return res;
}
#endif

void dataPrepare() {
    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    printCurrentTime();
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    uint8_t *packet = g_packet;
    memset(packet, 0, sizeof(g_packet));
    // 1. Заполняем заголовок и базовые поля
    size_t len = preparePacket(packet, 198, ID, Battery, dateBytes);

    // 2. Получаем данные измерений (предполагаем, что measure() возвращает
    // MeasureResult)
    MeasureResult mRes;

    for (int port = 0; port < 2; port++) {
        mRes = measure(activeport[port]); // или другой порт
        Serial.printf("Port %d: valid=%d, data=%p, len=%d\n", activeport[port],
                      mRes.valid, mRes.data, mRes.length);
        if (mRes.valid && mRes.data != nullptr && mRes.length > 0) {
            size_t remaining = sizeof(packet) - len;
            // Защита от выхода за границы 200-байтного буфера
            if (mRes.length > remaining) {
                Serial.printf(
                    "⚠️ Measured data too large (%d > %d). Truncating.\n",
                    mRes.length, remaining);
                mRes.length = remaining;
            }
            // Копируем измеренные данные СРАЗУ после заголовка
            memcpy(packet + len, mRes.data, mRes.length);
            len += mRes.length;
        } else {
            Serial.println("⚠️ No valid measured data to append");
        }
        if (mRes.valid && mRes.data != nullptr) {
            free(mRes.data);
            mRes.data = nullptr; // Защита от двойного free
        }
        yield();
    }
    // 3. Заполняем хвост нулями (если протокол требует фиксированные 200 байт)
    if (len < 198) {
        memset(packet + len, 0x00, 198 - len);
    }
    // 4. Вывод (для отладки печатаем только валидную часть, или все 200)
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

#endif

#if NET == 1 or NET == 2
void getNetTime() {
    Serial.println("Enabling time sync...");
    if (SimModule::enableTimeSync()) {
    }
    delay(10000); // Ждём NITZ-обновление от вышки
    for (int i = 0; i < 3; i++) {
        if (SimModule::syncSystemClock()) {
            Serial.println("✓ Time ready");
            blink(2, 1000);
            break;
        } else {
            Serial.println("fail");
            delay(3000);
        }
    }
    printCurrentTime();
}

void simres() {
    enable_power(true);
    Serial.println("sim pwr low");
    // digitalWrite(SIM_PWR, LOW);
    enable_sim(true);
    enable_sim(false);
    delay(1000);
    Serial.println("sim pwr high");

    // 2. Ждём загрузки модуля
    delay(10000);
    yield();

    // 3. Программная инициализация
    SimModule::begin();
    SimModule::activate(true);
    SimModule::factoryReset();
    SimModule::disconnect();
    SimModule::activate(false);
    SimModule::end();
}

bool processOneSMS(const String &text) {
    Serial.println("\n===== SMS =====");
    Serial.println(text);

    // Разбираем строку: ожидаем "ID pass broker port"
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
    String spass = parts[1];
    String sbroker = parts[2];
    uint16_t sport = parts[3].toInt();

    // Проверяем ID устройства (IDchar определён в defenitions.h)
    if (id != String(IDchar)) {
        Serial.printf("[SMS] ID не совпадает: SMS=%s, Device=%s\n", id.c_str(),
                      IDchar);
        return false;
    } else {
        saveConfigFromNVS(spass, sbroker, sport);
        return true;
    }
}
void readSMS() {
    String sms = "  ";
    Serial.println("read sms");
    while (sms != "") {
        sms = SimModule::getUnreadSMS();
        if (processOneSMS(sms)) {
            Serial.println("sms ok");
            // byte aa[] = {0x0A, 0x11, 0x11, 0x11, 0x11, 0x11,
            //              0x11, 0x11, 0x11, 0x11, 0x11};
            // // res.data[s] = (byte)port;
            // memcpy(g_packet, aa, (int)aa[0]);
            // printHEX(g_packet, 198);
            // stack.write(g_packet);
        }
    }
    Serial.println("ALL SMS READED");
}

bool sim_activate(bool act) {
    if (!act) {
        // Выключение
        SimModule::disconnect();
        SimModule::activate(false);
        SimModule::end();
        // enable_sim(false);
        enable_power(false);

        activate_sim(false);

        Serial.println("\tsim disconnected");
        return true;
    }

    // --- Включение с несколькими раундами ---
    const int MAX_ROUNDS = 2;               // максимум 3 попытки
    const unsigned long ROUND_DELAY = 3000; // 5 секунд между раундами
    Serial.println("try conect0");

    // activate_sim(false);

    for (int round = 1; round <= MAX_ROUNDS; round++) {
        Serial.printf("=== Round %d/%d ===\n", round, MAX_ROUNDS);
        yield();

        // activate_sim(true);

        enable_power(1);
        delay(2000);
        // enable_sim(1);
        // // 2. Ждём загрузки модуля
        // delay(10000);
        esp_task_wdt_reset();

        yield();
        // 3. Программная инициализация
        SimModule::begin();
        esp_task_wdt_reset();
        yield();

        SimModule::activate(true);
        // 4. Даём время на регистрацию в сети (можно просто задержку, т.к.
        // connect() сам ждёт)
        //    Но мы всё равно делаем небольшую паузу, чтобы модуль "осмотрелся"
        delay(5000);
        Serial.println("try conect");
        // 5. Пробуем подключиться
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

        // Неудача в этом раунде – полностью выключаем модуль перед следующей
        // попыткой
        Serial.println("Round failed, powering off...");
        SimModule::disconnect();
        SimModule::activate(false);
        SimModule::end();
        // enable_sim(false);
        delay(ROUND_DELAY);
    }

    Serial.println("All rounds failed");
    return false;
}

void SIM_check_signal() {
    Serial.printf(">>> Action 1 (GPIO )");
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

void SIM_reset() {
    enable_power(1);
    enable_sim(1);
    delay(5000);
    SimModule::begin();
    SimModule::activate(true);

    // Сброс к заводским (опционально, только при проблемах)
    if (SimModule::factoryReset()) {
        Serial.println("Modem reset done. Reconnecting...");
    }
    enable_sim(0);

    enable_power(0);
}

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

// bool mqtt_send() {
//     yield();
//     if (SimModule::mqttConnect(Broker, Port, Pass)) {
//         int l = stack.count();
//         Serial.printf("packets to sending -  %i\n", l);
//         for (int i = 0; i < l; i++) {
//             yield();
//             if (stack.read(g_packet)) {
//                 Serial.printf("    Popped packet #%d\n", i);
//                 printHEX(g_packet, (int)g_packet[0] + 1);
//                 int len = adding();
//                 Serial.printf("   Prepared packet #%d, len %i\n", i, len);
//                 printHEX(a_packet, len);
//                 if (SimModule::mqttSendPacket(a_packet, len)) {
//                     Serial.printf("   ✅ packet #%d sended\n", i);
//                     blink(1, 500);
//                 } else {
//                     stack.write(g_packet);
//                     SimModule::mqttDisconnect();
//                     return false;
//                 }
//             } else {
//                 Serial.printf("❌ Failed to pop packet #%d (stack empty?)\n",
//                               i);
//             }
//         }
//         yield();
//         SimModule::mqttDisconnect();
//         return true;

//     } else {
//         blink(10, 250);
//         SimModule::mqttDisconnect();
//         return false;
//     }
// }

bool restart_http() {
    SimModule::httpEnd();
    for (int q = 0; q < 3; q++) {
        if (!SimModule::httpBegin("158.160.78.215")) {
            Serial.println("Не удалось подключиться к серверу");
        } else {
            return true;
        }
    }
    return false;
}
bool http_send() {
    yield();

    int l = stack.count();
    Serial.printf("packets to sending -  %i\n", l);
    Serial.println(restart_http());
    for (int i = 0; i < l; i++) {
        yield();
        if (stack.read(g_packet)) {
            Serial.printf("    Popped packet #%d\n", i);
            printHEX(g_packet, (int)g_packet[0] + 1);
            int len = adding();
            Serial.printf("   Prepared packet #%d, len %i\n", i, len);
            printHEX(a_packet, len);
            if (SimModule::httpSendPacketSafe(a_packet, len, "device-001",
                                              "/gateway/packets",
                                              "158.160.78.215")) {
                Serial.printf("   ✅ packet #%d sended\n", i);
                blink(1, 500);
            } else {
                Serial.printf("   ❌ packet #%d NOT sended!!!\n", i);
                stack.write(g_packet);
                // Serial.println(restart_http());
            }
        } else {
            Serial.printf("❌ Failed to pop packet #%d (stack empty?)\n", i);
        }
    }

    SimModule::httpEnd();
    yield();
    return true;
}
#endif

#if NET == 0 or NET == 2
void lora_activate(bool act) {
    if (act) {
        enable_lora(1);
        delay(1000);
        Serial.printf("LoRa:: begin, %i\n", LoRa::begin());
    } else {
        Serial.printf("LoRa:: begin, %i\n", LoRa::end());

        enable_lora(0);
        delay(1000);
    }
}
void LORA_sendOK() {
    byte date[] = {0x00, 0x01, 0x01, 0x00, 0x00, 0x00};
    if (isTime) {
        getPackedTimeBytes(date);
    }
    printCurrentTime();
    printHEX(date, 6);
    byte pac[] = {rxBuffer[1], rxBuffer[2], rxBuffer[3], 0xff,    date[0],
                  date[1],     date[2],     date[3],     date[4], date[5]};
    printHEX(pac, 10);

    LoRa::send(pac, 10);
}
void lora_send() {
    const unsigned long SESSION_TIMEOUT =
        120000; // 2 минуты на сеанс (было 5, можно оставить 120с)
    const unsigned long ACK_TIMEOUT = 5000; // ожидание ACK 5 с
    const unsigned long RETRY_DELAY_MS =
        10000; // пауза между попытками одного пакета 10 с
    const int MAX_ATTEMPTS_PER_PACKET = 2; // попыток на один пакет

    unsigned long sessionStart = millis();
    lora_activate(true);
    Serial.printf("Stack count %i\n", stack.count());
    while (stack.count() > 0) {
        // Проверка общего таймаута сеанса
        if (millis() - sessionStart > SESSION_TIMEOUT) {
            Serial.println("Session timeout");
            break;
        }

        // Читаем первый пакет (удаляем из стека)
        if (!stack.read(g_packet)) {
            Serial.println("Read failed – stack empty?");
            break;
        }

        Serial.printf(">>> Sending packet [len=%d]\n", g_packet[0]);
        bool success = false;
        printHEX(g_packet, g_packet[0]);
        // Попытки отправки текущего пакета
        for (int attempt = 0; attempt < MAX_ATTEMPTS_PER_PACKET; attempt++) {
            if (millis() - sessionStart > SESSION_TIMEOUT)
                break;

            if (!LoRa::send(g_packet, (int)g_packet[0])) {
                Serial.printf("  LoRa send error, attempt %d\n", attempt + 1);
                delay(RETRY_DELAY_MS);
                continue;
            }

            // Ожидание ACK с корректным ID и 4-м байтом 0xFF
            bool ackReceived = false;
            unsigned long ackStart = millis();
            while (millis() - ackStart < ACK_TIMEOUT) {
                int len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
                if (len > 0 && rxBuffer[3] == 0xFF) {
                    if (rxBuffer[0] == (byte)(ID >> 16) & 0xFF &&
                        rxBuffer[1] == (byte)(ID >> 8) & 0xFF &&
                        rxBuffer[2] == (byte)(ID) & 0xFF) {
                        ackReceived = true;
                        break;
                    }
                    // чужой пакет – игнорируем, продолжаем слушать
                }
                delay(10);
            }

            if (ackReceived) {
                success = true;
                Serial.println("  ACK OK");
                // Опционально: установка времени
                byte dateBytes[6] = {rxBuffer[4], rxBuffer[5], rxBuffer[6],
                                     rxBuffer[7], rxBuffer[8], rxBuffer[9]};
                if (setTimeFromHexBytes(dateBytes)) {
                    printCurrentTime();
                }
                blink(1, 250); // индикация успеха
                break;         // выходим из цикла попыток
            }

            Serial.printf("  No ACK, attempt %d\n", attempt + 1);
            delay(RETRY_DELAY_MS);
        }

        if (!success) {
            // Пакет не удалось отправить – возвращаем в конец стека
            if (stack.write(g_packet)) {
                Serial.println("  Returned to stack tail");
            } else {
                Serial.println("  Stack full, packet lost!");
            }
            // Прерываем передачу, не пробуем другие пакеты
            Serial.println("  Failed to send first packet, ending session.");
            blink(10, 250);
            break;
        }
        // При успехе пакет уже удалён (read), продолжаем со следующим
        blink(1, 250); // небольшая пауза между успешными отправками
    }
    lora_activate(false);
}

int lora_rssi(byte *pac) {
    int len = 0;
    if (LoRa::send(pac, pac[0])) {
        for (int i = 0; i < 100; i++) {
            len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
            if (len > 0) {
                printHEX(rxBuffer, len);
                if (len > 0 and rxBuffer[3] == 0xff) {
                    if (rxBuffer[0] == (byte)(ID >> 16) & 0xFF and
                        rxBuffer[1] == (byte)(ID >> 8) & 0xFF and
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

    // lora_activate(true);

    // int ch = readSwitchState() + 1;
    // Serial.printf("Chanel set %i\n", ch);
    // blink(ch, 400);

    // if (LoRa::configSet(17, ch)) {
    // LoRa::configGet();
    // }
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
    // lora_activate(false);
}
#endif

RTC_DATA_ATTR int wakeups = 0;

#if BOARD_TYPE == 0 and NET == 1
void work() {
    if (tableSens[activeport[0]] != 0x00 or tableSens[activeport[1]] != 0x00) {
        dataPrepare();
        enable_sens(0);
        if (stack.count() > 0) {
            if (sim_activate(true)) {
                Serial.println("coneceted do mqtt");
                mqtt_send();
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
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
    Battery = readBatteryVoltage();

    uint32_t cpu_mhz = getCpuFrequencyMhz(); // частота CPU в МГц
    uint32_t apb_mhz = getApbFrequency();    // частота шины APB (обычно 80 МГц)

    Serial.printf("CPU Frequency: %u MHz\n", cpu_mhz);
    Serial.printf("APB Frequency: %u Hz\n", apb_mhz);

    RsModbus::init(REDE);
    delay(1000);
    if (!loadArrayFromFlash(tableSens)) {
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00; // Заполняем 0,1,2...7
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
    // byte simpl[] = {0x00, 0x24, 0x00, 0x00, 0x07};
    // saveArrayToFlash(simpl);

    switch (wake_but) {
    case 1:
        work();
        // SIM_check_signal();
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
    // sleep(30);
    sleep(TIME_TO_SLEEP);
}
void loop() {}
#endif
#if BOARD_TYPE == 0 and NET == 0 and BOARD_REV == 3
void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
    uint32_t cpu_mhz = getCpuFrequencyMhz(); // частота CPU в МГц
    uint32_t apb_mhz = getApbFrequency();    // частота шины APB (обычно 80 МГц)

    Serial.printf("CPU Frequency: %u MHz\n", cpu_mhz);
    Serial.printf("APB Frequency: %u Hz\n", apb_mhz);
    Battery = readBatteryVoltage();
    RsModbus::init(REDE);
    delay(1000);
    if (!loadArrayFromFlash(tableSens)) {
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00; // Заполняем 0,1,2...7
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
    byte pac[198] = {0};
    int q = 0;
    switch (wake_but) {
    case 1:
        lora_check_signal();
        sleep(10);
        break;
    case 2:
        searchSensors(1);
        searchSensors(4);
        sleep(10);
        break;
    case 3:
        Serial.println("work first");
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00;
        }
        saveArrayToFlash(tableSens);
        cleanUpStack();
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
        Serial.println("            complited");
        break;
    }
    sleep(TIME_TO_SLEEP);
}
void loop() {}
#endif

#if BOARD_TYPE == 0 and NET == 0
void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
    uint32_t cpu_mhz = getCpuFrequencyMhz(); // частота CPU в МГц
    uint32_t apb_mhz = getApbFrequency();    // частота шины APB (обычно 80 МГц)

    Serial.printf("CPU Frequency: %u MHz\n", cpu_mhz);
    Serial.printf("APB Frequency: %u Hz\n", apb_mhz);
    Battery = readBatteryVoltage();
    RsModbus::init(REDE);
    delay(1000);
    if (!loadArrayFromFlash(tableSens)) {
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00; // Заполняем 0,1,2...7
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
    byte pac[198] = {0};
    int q = 0;
    switch (wake_but) {
    case 1:
        lora_check_signal();
        sleep(10);
        break;
    case 2:
        searchSensors(1);
        searchSensors(4);
        sleep(10);
        break;
    case 3:
        Serial.println("work first");
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00;
        }
        saveArrayToFlash(tableSens);
        // cleanUpStack();
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
        Serial.println("            complited");
        break;
    }
    sleep(TIME_TO_SLEEP);
}
void loop() {}
#endif

#if BOARD_TYPE == 1 and NET == 2 and BOARD_REV == 3
// #include <esp_attr.h>   // RTC_DATA_ATTR

RTC_DATA_ATTR uint32_t lastWorkTime = 0;
RTC_DATA_ATTR uint32_t lastSleepTime = 0;
const uint32_t WORK_INTERVAL = 30UL * 60 * 1000; // 30 минут
int iter = 0;

RTC_DATA_ATTR int test2_phase = 0;     // 0=заполнение, 1=после краша
RTC_DATA_ATTR bool test2_crashed = false;
// ----- Вспомогательная функция записи одного пакета -----
void writeTestPacket(uint8_t marker, uint8_t value) {
    uint8_t aa[] = {0x0A, 0x11, 0x11, 0x11, 0x11, 0x11,
                    0x11, 0x11, 0x11, 0x11, 0x11};
    aa[1] = marker;
    for (int q = 2; q < 8; q++) {
        aa[q] = value;
    }
    memcpy(g_packet, aa, aa[0]);
    stack.write(g_packet);
}

void work() {
    bool success = false;
    if (sim_activate(true)) {
        printCurrentTime();
        Serial.println("    TRY do mqtt");
        if (http_send()) {
            Serial.println("    ✅ SENDING COMPLETE");
            success = true;
        } else {
            Serial.println("   ❌ SENDING FAIL");
        }

    } else {
        Serial.println("   ❌ SIM activation failed");
    }
    sim_activate(false);
}

void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
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
        // simres();
        // activate_sim(false);
        // cleanUpStack();
        // activate_sim(false);

        // SIM_check_signal();
        // enable_sim(0);
        // enable_power(0);
        // lora_activate(true);
        // int ch = readSwitchState() + 1;
        // Serial.printf("Chanel set %i\n", ch);
        blink(1, 400);
        // if (LoRa::configSet(17, ch)) {
        //     LoRa::configGet();
        // }
        // lora_activate(0);
    }
    if (test2_phase == 0) {
        // Первый запуск: заполняем буфер 20 пакетами
        stack.clear();
        for (int i = 1; i <= 20; i++) {
            writeTestPacket(0, i); // marker=0, value=1..20
        }
        Serial.println("Test2: 20 records written");
        test2_phase = 1;
        test2_crashed = false;
        delay(100);
        // Имитируем нормальную перезагрузку (можно просто уйти в сон и
        // проснуться, но для чистоты эксперимента сделаем программный сброс)
        sleep(10);
    }

    if (test2_phase == 1 && !test2_crashed) {
        // Пробуждение до сбоя: читаем первые 7 пакетов
        for (int i = 0; i < 7; i++) {
            if (stack.read(g_packet)) {
                Serial.printf("Test2 pre-crash read #%d: ", i);
                printHEX(g_packet, g_packet[0] + 1);
            } else {
                Serial.println("Test2 ERROR: failed to read packet");
            }
        }
        // Имитируем внезапное отключение питания
        test2_crashed = true;
        Serial.println("Test2: ***** POWER FAILURE *****");
        delay(100);
        sleep(10); // полный сброс контроллера
    }

    if (test2_phase == 1 && test2_crashed) {
        // После сбоя: смотрим, что осталось в буфере
        Serial.printf("Test2 after crash: stack.count() = %u\n", stack.count());
        // Читаем все оставшиеся пакеты и выводим их
        while (stack.count() > 0) {
            if (stack.read(g_packet)) {
                Serial.print("Test2 remaining: ");
                printHEX(g_packet, g_packet[0] + 1);
            }
        }
        test2_phase = 2; // тест завершён
        Serial.println("Test2: verification done");
    }

    if (test2_phase == 2) {
        // Бесконечный глубокий сон, чтобы не тратить батарею
        Serial.println("Test2 finished, entering deep sleep forever...");
        delay(100);
        sleep(10);
    }

    // work();
    uint32_t now = millis();
    if (lastWorkTime == 0)
        lastWorkTime = now;
    if (lastSleepTime == 0)
        lastSleepTime = now;
    // lora_activate(true);
}

void loop() {
    // Приём LoRa пакетов
    int len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
    if (len > 0) {
        digitalWrite(LED_PIN, HIGH);
        Serial.printf("\n\n📨 Packet %d bytes\n", len);
        printHEX(rxBuffer, (int)rxBuffer[0] + 1);
        byte senstime[] = {rxBuffer[5], rxBuffer[6], rxBuffer[7],
                           rxBuffer[8], rxBuffer[9], rxBuffer[10]};
        printTimeFromHexBytes(senstime);
        bool crcOK = checkCRC(rxBuffer, (int)rxBuffer[0]);
        if (crcOK && len > 15) {
            Serial.println("✅Valid sensor data");
            LORA_sendOK();
            uint8_t *packet = g_packet;
            memset(packet, 0, sizeof(g_packet));
            memcpy(packet, rxBuffer, len);
            if (stack.write(packet)) {
                Serial.printf("pushed len %i, count %i\n", len, stack.count());
            }
        }
        if (!crcOK && len < 15) {
            Serial.println("📣Sensor signal check");
            LORA_sendOK();
        }
        digitalWrite(LED_PIN, LOW);
    }

    uint32_t now = millis();
    bool timeExpired = (now - lastWorkTime >= WORK_INTERVAL);
    bool timeToSleep = (now - lastSleepTime >= WORK_INTERVAL * 2.1);

    // Условие для запуска work(): есть пакеты и (истекло время или кнопка)
    if (stack.count() > 0 && (timeExpired || digitalRead(BUT2) == LOW)) {
        blink(2, 750);
        Serial.printf("stack count %i timeExpired %i\n", stack.count(),
                      timeExpired);
        Serial.println("️        Running work()...");
        work();                  // теперь work() не уходит в сон внутри
        lastWorkTime = millis(); // обновляем метку после попытки
        blink(2, 750);
        Serial.println("️        END work()...");
        lora_activate(true);
    }

    // Плановый уход в глубокий сон
    // if (timeToSleep) {
    //     lastSleepTime = now;
    //     sleep(10); // 30 минут
    // }

    delay(10);
    yield();
}
#endif

#if BOARD_REV == 1 and BOARD_TYPE == 1
RTC_DATA_ATTR uint32_t lastWorkTime = 0;
RTC_DATA_ATTR uint32_t lastSleepTime = 0;
const uint32_t WORK_INTERVAL = 30UL * 60 * 1000; // 30 минут
int iter = 0;
void work() {
    lora_activate(false);
    // enable_power(false);
    delay(100);
    Battery = readBatteryVoltage();

    bool success = false;
    if (sim_activate(true)) {
        printCurrentTime();
        Serial.println("    TRY do mqtt");
        if (mqtt_send()) {
            Serial.println("    ✅ SENDING COMPLETE");
            success = true;
        } else {
            Serial.println("   ❌ SENDING FAIL");
        }

    } else {
        Serial.println("   ❌ SIM activation failed");
    }
    sim_activate(false);
}

void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    // Battery = readBatteryVoltage();

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
        // simres();
        // activate_sim(false);
        // cleanUpStack();
        activate_sim(false);

        SIM_check_signal();
        // enable_sim(0);
        // enable_power(0);
        lora_activate(true);
        // int ch = readSwitchState() + 1;
        // Serial.printf("Chanel set %i\n", ch);
        // blink(ch, 400);
        // if (LoRa::configSet(17, ch)) {
        //     LoRa::configGet();
        // }
        // lora_activate(0);
    }
    // byte aa[] = {0x0A, 0x11, 0x11, 0x11, 0x11, 0x11,
    //              0x11, 0x11, 0x11, 0x11, 0x11};
    // // res.data[s] = (byte)port;
    // memcpy(g_packet, aa, (int)aa[0]);
    // printHEX(g_packet, 198);
    // stack.write(g_packet);
    // blink(2, 750);
    // uint32_t now = millis();
    // if (lastWorkTime == 0)
    //     lastWorkTime = now;
    // if (lastSleepTime == 0)
    //     lastSleepTime = now;
    // lora_activate(true);
}

void loop() {
    // // Приём LoRa пакетов
    // int len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
    // if (len > 0) {
    //     digitalWrite(LED_PIN, HIGH);
    //     Serial.printf("\n\n📨 Packet %d bytes\n", len);
    //     printHEX(rxBuffer, (int)rxBuffer[0] + 1);
    //     byte senstime[] = {rxBuffer[5], rxBuffer[6], rxBuffer[7],
    //                        rxBuffer[8], rxBuffer[9], rxBuffer[10]};
    //     printTimeFromHexBytes(senstime);
    //     bool crcOK = checkCRC(rxBuffer, (int)rxBuffer[0]);
    //     if (crcOK && len > 15) {
    //         Serial.println("✅Valid sensor data");
    //         LORA_sendOK();
    //         uint8_t *packet = g_packet;
    //         memset(packet, 0, sizeof(g_packet));
    //         memcpy(packet, rxBuffer, len);
    //         if (stack.write(packet)) {
    //             Serial.printf("pushed len %i, count %i\n", len,
    //             stack.count());
    //         }
    //     }
    //     if (!crcOK && len < 15) {
    //         Serial.println("📣Sensor signal check");
    //         LORA_sendOK();
    //     }
    //     digitalWrite(LED_PIN, LOW);
    // }

    // uint32_t now = millis();
    // bool timeExpired = (now - lastWorkTime >= WORK_INTERVAL);
    // bool timeToSleep = (now - lastSleepTime >= WORK_INTERVAL * 2.1);

    // // Условие для запуска work(): есть пакеты и (истекло время или
    // кнопка) if (stack.count() > 0 && (timeExpired || digitalRead(BUT2) ==
    // LOW)) {
    //     blink(2, 750);
    //     Serial.printf("stack count %i timeExpired %i\n", stack.count(),
    //                   timeExpired);
    //     Serial.println("️        Running work()...");
    //     work();                  // теперь work() не уходит в сон внутри
    //     lastWorkTime = millis(); // обновляем метку после попытки
    //     blink(2, 750);
    //     Serial.println("️        END work()...");
    //     lora_activate(true);
    // }

    // // Плановый уход в глубокий сон
    // // if (timeToSleep) {
    // //     lastSleepTime = now;
    // //     sleep(10); // 30 минут
    // // }

    delay(10);
    yield();
}
#elif BOARD_REV == 3 and BOARD_TYPE == 2
void setup() {
    // initPins();
    pinMode(LED_PIN, OUTPUT);
    pinMode(ELORA, OUTPUT);
    pinMode(BUT2, INPUT_PULLUP); // Кнопка NO: в покое HIGH, при нажатии LOW

    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
    Serial.begin(115200); // монитор порта
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

    // 4-й параметр = 1 → скрытая сеть

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssidwifi, passwifi);
    delay(1000); // Ждём стабилизации AP

    // Страница с автообновлением
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

    // Отдача лога
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
#endif
