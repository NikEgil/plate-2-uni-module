#include "FlashStack.h"
#include <Arduino.h>
#if BOARD_REV == 2
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
#if BOARD_REV == 2

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

int Battery = 146;
byte tableSens[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
byte ccid[10] = {};
int signalp = 0;
void cleanUpStack() {
    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
        return;
    }

    // 🔹 2. Удаляем старые файлы стека (выполнить ОДИН РАЗ после смены
    // RECORD_SIZE)
    if (LittleFS.exists("/stack.dat")) {
        Serial.println("🗑 Removing old stack.dat (RECORD_SIZE changed)");
        LittleFS.remove("/stack.dat");
    }
    if (LittleFS.exists("/stack.meta")) {
        Serial.println("🗑 Removing old stack.meta");
        LittleFS.remove("/stack.meta");
    }
    LittleFS.format();
    // 🔹 3. Инициализируем стек с новыми настройками
    if (stack.begin()) {
        Serial.printf("✅ Stack initialized. RECORD_SIZE=%d, count=%d\n",
                      RECORD_SIZE, stack.count());
    }
    blink(2, 750);
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
    digitalWrite(LED_PIN, HIGH);
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

    // if (searche_multisens()) {
    //     tableSens[port] = 0x01;
    //     saveArrayToFlash(tableSens);
    //     blink(1, 1000);
    //     return true;
    // }
    Serial.println("founding sens");

    // Serial.println("	request:");
    byte req[] = {0xFF, 0x03, 0x07, 0xD0, 0x00, 0x01, 0x91, 0x59};

    int lenreq = 8;
    printHEX(req, lenreq);
    RsModbus::sendData(req, sizeof(req));
    delay(500);

    byte response[32] = {0};
    // Ждём ответ 150 мс (для Modbus обычно хватает 50-100 мс)
    size_t lenresponse = RsModbus::receiveData(response, sizeof(response), 10);
    // Serial.println("	responses:");
    printHEX(response, lenresponse);

    // if (response[0] == 0x00) {
    //     Serial.println("	wait meteo:");
    //     for (int i = 0; i < 80; i++) {
    //         delay(250);
    //         lenresponse = RsModbus::receiveData(response, sizeof(response),
    //         10); printHEX(response, lenresponse); if (response[0] != 0) {
    //             break;
    //         }
    //     }
    // }

    Serial.printf("%i		FOUND		!!!\n", (int)response[0]);
    if (response[0] == 0x24) {
        Serial.println("meteostation detected");
    } else if (response[0] != 0x00) {
        // Serial.println("another sens detected");
    } else {
        Serial.println("		NOT SENS	!!!");
    }

    tableSens[port] = response[0];
    saveArrayToFlash(tableSens);
    enable_sens(0);
    enable_power(0);
    digitalWrite(LED_PIN, LOW);
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
    for (int i = 0; i < 80; i++) {
        received = RsModbus::receiveData(outBuf, outBufSize, 300);
        Serial.printf("Tray %i,  RX[%d]: ", i, received);
        printHEX(outBuf, received);
        Serial.printf("rec %i,  sensreg %i: ", received, lenreg);
        if (received == 21) {

            return received;
            printHEX(outBuf, received);
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
#if BOAD_REV == 3
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
    Serial.println("✅ Measured");
    digitalWrite(LED_PIN, LOW);
    return res;
}

#elif BOARD_REV == 2
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
    Serial.printf("Total packed length: %d bytes\n", len);
    packet[0] = (byte)(len + 2);
    addCRC(packet, len, packet);
    printHEX(packet, len + 2);
    yield();
    if (stack.push(packet)) {
        Serial.printf("   ✅ Pushed packet (count: %d)\n", stack.count());
    } else {
        Serial.printf("   ❌ Failed to push packet #%d (stack full?)\n",
                      stack.count());
    }
    yield();
    enable_sens(0);
    enable_power(0);
}

void Stack_and_sensors() {
    Serial.println(">>> Action 2 (GPIO 9)");
    blink(2, 1500);
    cleanUpStack();
    searchSensors(activeport[0]);
    searchSensors(activeport[1]);
}
#endif
#if NET == 1 or NET == 2
void getNetTime() {

    Serial.println("Enabling time sync...");
    if (SimModule::enableTimeSync()) {
    }
    delay(2000); // Ждём NITZ-обновление от вышки
    for (int i = 0; i < 6; i++) {
        if (SimModule::syncSystemClock()) {
            Serial.println("✓ Time ready");
            blink(2, 1000);
            break;
        } else {
            delay(5000);
        }
    }

    printCurrentTime();
}

bool sim_activate(bool act) {

    if (act) {
        yield();
        enable_power(true);
        delay(2000);
        enable_sim(true);
        // enable_sens(0);
        SimModule::begin();
        SimModule::activate(true);
        //  SimModule::ccid(ccid);
        delay(5000);
        yield();

        if (SimModule::connect(apn, gprsUser, gprsPass)) {
            Serial.println("	sim connected");
            signalp = SimModule::getSignalQuality();
            SimModule::ccid(ccid);
            printHEX(ccid, 10);
            printCurrentTime();
            if (!isTime()) {
                getNetTime();
            }
            blink(1, 1000);
            return true;
        } else {
            return false;
        }
    } else {
        // if (SimModule::isConnection()) {
        SimModule::disconnect();
        // }
        enable_sim(false);
        enable_power(false);
        Serial.println("	sim disconnected");
        return true;
    }
}

void SIM_check_signal() {
    Serial.printf(">>> Action 1 (GPIO %i)", BUT1);
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

bool mqtt_send() {
    yield();
    if (SimModule::mqttConnect()) {
        int l = stack.count();
        Serial.printf("packets to sending -  %i\n", l);
        for (int i = 0; i < l; i++) {
            yield();
            if (stack.pop(g_packet)) {
                Serial.printf("    Popped packet #%d\n", i);
                printHEX(g_packet, (int)g_packet[0] + 1);
                int len = adding();
                Serial.printf("   Prepared packet #%d, len %i\n", i, len);
                printHEX(a_packet, len);
                if (SimModule::mqttSendPacket(a_packet, len)) {
                    Serial.printf("   ✅ packet #%d sended\n", i);
                    blink(1, 500);
                } else {
                    stack.push(g_packet);
                    SimModule::mqttdisconnect();
                    return false;
                }
            } else {
                Serial.printf("❌ Failed to pop packet #%d (stack empty?)\n",
                              i);
            }
        }
        yield();
        SimModule::mqttdisconnect();
        return true;

    } else {
        blink(10, 250);
        return false;
    }
}

#elif NET == 0 or NET == 2
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
void lora_send(int l = 0) {
    yield();
    bool da = false;
    lora_activate(true);
    // int l = stack.count();
    if (stack.count() > 5) {
        l = 5;
    } else {
        l = stack.count();
    }
    Serial.printf("всего пакетов %i\n", l);
    for (int i = 0; i < l; i++) {
        if (stack.pop(g_packet)) {
            Serial.printf("   ✅ Popped packet #%d\n", i);
            printHEX(g_packet, (int)g_packet[0]);
            int len = 0;
            if (LoRa::send(g_packet, (int)g_packet[0])) {
                for (int k = 0; k < 100; k++) {
                    len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
                    if (len > 0) {
                        printHEX(rxBuffer, len);
                        float rssi = (float)rxBuffer[len - 1];
                        Serial.println(rssi);
                        int16_t dbm = (int)-(rssi / 2);
                        int perc = (uint8_t)(((dbm + 128) * 100) / 108);
                        if (len > 0 and rxBuffer[3] == 0xff) {
                            if (rxBuffer[0] == (byte)(ID >> 16) & 0xFF and
                                rxBuffer[1] == (byte)(ID >> 8) & 0xFF and
                                rxBuffer[2] == (byte)(ID) & 0xFF) {
                                Serial.println(" ID OK");
                                Serial.printf("rssi get %f,perc %i", rssi,
                                              perc);
                                byte dateBytes[6] = {rxBuffer[4], rxBuffer[5],
                                                     rxBuffer[6], rxBuffer[7],
                                                     rxBuffer[8], rxBuffer[9]};
                                if (setTimeFromHexBytes(dateBytes)) {
                                    printCurrentTime();
                                }
                                blink(1, 2000);
                                da = true;
                                break;
                            } else {
                                break;
                            }
                        }
                    } else {
                        Serial.print('.');
                    }
                    delay(50);
                }
            }
        } else {
            Serial.printf("   ❌ Failed to pop packet #%d (stack empty?)\n", i);
        }
        if (!da) {
            Serial.printf("   ❌ Failed sending");
            stack.push(g_packet);
            blink(10, 250);
            yield();
            lora_activate(false);
            sleep(TIME_TO_SLEEP);
        }
    }

    yield();
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
    Serial.printf(">>> Action 1 (GPIO %i)", BUT1);
    blink(1, 1500);
    byte pac[11] = {};

    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    Serial.println("da");
    size_t len = preparePacket(pac, 11, ID, Battery, dateBytes);
    pac[0] = (byte)(len);

    lora_activate(true);
    if (LoRa::configSet(17, 1)) {
        LoRa::configGet();
    }
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
#endif

#if BOARD_TYPE == 0 and NET == 1
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
    Serial.printf("         BATA %i", Battery);
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

    uint8_t wake_but = checkButton();
    Serial.printf("State wake up %i\n\n", wake_but);

    switch (wake_but) {
    case 1:
        SIM_check_signal();
        break;

    case 2:
        Stack_and_sensors();
        break;
    default:
        // Stack_and_sensors();
        dataPrepare();
        if (sim_activate(true)) {
            Serial.println("coneceted do mqtt");
            mqtt_send();
            sim_activate(false);
            enable_sens(0);
            enable_power(0);
        }
        Serial.println("            complited");
        break;
    }
    // sleep(30);
    sleep(TIME_TO_SLEEP);
}
void loop() {}
#elif BOARD_TYPE == 0 and NET == 0 and BOARD_REV == 3
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

    uint8_t wake_but = checkButton();
    Serial.printf("State wake up %i\n\n", wake_but);

    switch (wake_but) {
    case 1:
        lora_check_signal();
        break;
    case 2:
        Stack_and_sensors();
        break;
    default:
        // lora_check_signal();
        dataPrepare();
        lora_send();

        Serial.println("            complited");
        Serial.println("one run");
        break;
    }
    // sleep(30);
    sleep(TIME_TO_SLEEP);
}
void loop() {}
#elif BOARD_TYPE == 1 and BOARD_REV == 3

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
    enable_power(0);
    enable_sim(0);
}
static uint32_t lastWorkTime = 0;
const uint32_t WORK_INTERVAL = 30UL * 60 * 1000; // 20 минут
int iter = 0;

void work() {
    lora_activate(false);
    enable_power(0);
    delay(100);
    Battery = readBatteryVoltage();
    enable_power(1);

    if (sim_activate(true)) {
        printCurrentTime();
        Serial.println("    TRY do mqtt");
        if (mqtt_send()) {
            Serial.println("    ✅ SENDING COMLITE");
        } else {
            Serial.println("   ❌ SENDING FAIL");
        }
        sim_activate(false);
    }
    lora_activate(true);
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
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("⚠️ Last reset: %d (4=WDT, 5=Brownout)\n", reason);
    Serial.printf("Rev: %d, NET: %d\n", BOARD_REV, NET);
    int b = checkButton();
    if (b == 1) {
        SIM_check_signal();
    }
    if (b == 2) {
        cleanUpStack();
    }

    if (stack.begin()) {
        Serial.printf("✅ Stack initialized. Current records: %d\n",
                      stack.count());
    } else {
        Serial.println("❌ Failed to init FlashStack!");
    }
    blink(5, 750);
    uint32_t now = millis();

    lastWorkTime = now;

    lora_activate(true);
}

void loop() {
    int len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));
    if (len > 0) {
        digitalWrite(LED_PIN, HIGH);
        Serial.printf("\n\n📨 Packet %d bytes\n", len);
        printHEX(rxBuffer, (int)rxBuffer[0] + 1);
        byte senstime[] = {rxBuffer[5], rxBuffer[6], rxBuffer[7],
                           rxBuffer[8], rxBuffer[9], rxBuffer[10]};
        printTimeFromHexBytes(senstime);
        byte crc[2] = {};
        bool crcOK = checkCRC(rxBuffer, (int)rxBuffer[0]);
        if (crcOK and len > 15) {
            Serial.println("✅Valid sensor data");
            LORA_sendOK();
            uint8_t *packet = g_packet;
            memset(packet, 0, sizeof(g_packet));
            memcpy(packet, rxBuffer, len);

            if (stack.push(packet)) {
                Serial.printf("pushed len %i, count %i\n", len, stack.count());
            }
        }
        if (!crcOK and len < 15) {
            Serial.println("📣Sensor signal check");
            LORA_sendOK();
        }
        digitalWrite(LED_PIN, LOW);
    }

    uint32_t now = millis();
    bool timeExpired = (now - lastWorkTime >= WORK_INTERVAL);

    // Запускаем work(), если сработал триггер И прошла минимальная пауза
    if (stack.count() > 0 and timeExpired) {
        Serial.printf("stack count %i timeExpired %i \n", stack.count(),
                      timeExpired);
        // Сбрасываем отсчёт
        Serial.println("️        Running work()...");
        work(); // Блокирующий вызов SIM800
        Serial.println("️        END work()...");
        lastWorkTime = now;
    }

    delay(10);
    yield();
}

#elif BOARD_REV == 2
void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    // Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
    // Battery = readBatteryVoltage();
    // Serial.printf("         BATA %i", Battery);
    RsModbus::init(REDE);
    delay(1000);
    if (!loadArrayFromFlash(tableSens)) {
        for (int i = 0; i < 5; i++) {
            tableSens[i] = 0x00; // Заполняем 0,1,2...7
        }
        saveArrayToFlash(tableSens);
    }

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
    searchSensors(1);
    searchSensors(4);
    enable_sens(0);
    enable_power(0);
}

int i = 0;
float bytesToInt(uint8_t high, uint8_t low) { return ((int)high << 8) | low; }

void loop() {
    int bat = checkButton();
    if (bat != 0) {
        int b = (int)(((readBatteryVoltage() - 25) * 100) / 18);
        Serial.printf("\n\nBattery  %i %%\n", b);

        MeasureResult mRes= {nullptr, 0, false};
        if (bat == 2 and tableSens[1]!=0x00) {
            // MeasureResult res = {nullptr, 0, false};
            mRes = measure(1); // или другой порт
            // Serial.printf("Port %d: valid=%d, data=%p, len=%d\n",
            // mRes.valid,mRes.data, mRes.length);
        }
        if (bat == 1 and tableSens[4]!=0x00) {
            mRes = measure(4); // или другой порт
            // Serial.printf("Port %d: valid=%d, data=%p, len=%d\n",
            // mRes.valid,mRes.data, mRes.length);
        }
        printHEX(mRes.data, mRes.length);
        if (mRes.valid and  mRes.data[1] == 0x1E) {
            Serial.printf("🌡️\tAir temperature - %.1f °C\n",
                          bytesToInt(mRes.data[6], mRes.data[7]) / 10);
            Serial.printf("💧\tAir humidity - %.1f %% \n",
                          bytesToInt(mRes.data[4], mRes.data[5]) / 10);
            Serial.printf("☀️\tIllumination - %.0f Lux\n",
                          bytesToInt(mRes.data[8], mRes.data[9]));
        }

        if (mRes.valid and  mRes.data[1] == 0x07) {
            Serial.printf("🌡️\tSoil temperature - %.1f °C\n",
                          bytesToInt(mRes.data[6], mRes.data[7]) / 10);
            Serial.printf("💧\tSoil humidity - %.1f %% \n",
                          bytesToInt(mRes.data[4], mRes.data[5]) / 10);
            Serial.printf("⚡\tConductivity - %.0f us/cm\n",
                          bytesToInt(mRes.data[8], mRes.data[9]));
            Serial.printf("🧪\tpH - %.1f \n",
                          bytesToInt(mRes.data[10], mRes.data[11]/100) );
            // Serial.printf("🌾 Nitrogen (N) - %.0f us/cm\n",
            //               bytesToInt(mRes.data[12], mRes.data[13]) );
            // Serial.printf("🌻 Phosphorus (P) - %.0f us/cm\n",
            //               bytesToInt(mRes.data[14], mRes.data[15]));
            // Serial.printf("🍌 Potassium (K) - %.0f us/cm\n",
            //               bytesToInt(mRes.data[16], mRes.data[17]) );
        }
    }
    delay(200);
}
#endif
