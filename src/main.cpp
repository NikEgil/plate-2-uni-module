#include "FlashStack.h"
#include <Arduino.h>
#include <defenitions.h>
// #include <LoRa_E220.h>
// #include <HardwareSerial.h>

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

FlashStack stack;
uint8_t g_packet[198];
static uint8_t rxBuffer[250];

int Battery;
byte tableSens[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
RTC_DATA_ATTR int state = 1;
int rssi1 = 0;

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
        Serial.println("another sens detected");
    } else {
        Serial.println("		NOT SENS	!!!");
    }

    tableSens[port] = response[0];
    saveArrayToFlash(tableSens);
    enable_sens(0);
    enable_power(0);
    digitalWrite(LED_PIN, LOW);
    if (response[0] != 0x00) {

        blink((int)response[0], 250);
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
        Serial.printf("  RX[%d]: ", received - 1);
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

void dataPrepare() {
    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    printCurrentTime();
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    uint8_t *packet = g_packet;
    memset(packet, 0, sizeof(g_packet));
    // 1. Заполняем заголовок и базовые поля
    size_t len = preparePacket(packet, 198, ID, Battery, dateBytes, 0, 0);

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
}

void Stack_and_sensors() {
    Serial.println(">>> Action 2 (GPIO 9)");
    blink(1, 1600);
    blink(2, 750);
    cleanUpStack();
    searchSensors(activeport[0]);
    searchSensors(activeport[1]);
}

#elif BOARDTYPE == 1

#endif

#if NET == 1 or NET == 2
bool sim_activate(bool act) {
    if (SimModule::isConnection()) {
        return true;
    }
    if (act) {
        enable_power(true);
        enable_sim(true);
        // enable_sens(0);
        SimModule::begin();
        SimModule::activate(true);
        if (SimModule::connect(apn, gprsUser, gprsPass)) {
            Serial.println("	sim connected");
            rssi1 = SimModule::getSignalQuality();
            blink(1, 1000);
            return true;
        } else {
            return false;
        }
    } else {
        if (SimModule::isConnection()) {
            SimModule::disconnect();
        }
        enable_sim(false);
        Serial.println("	sim disconnected");
        return true;
    }
}

void getNetTime() {
    if (sim_activate(true)) {
        // delay(10000);
        Serial.println("Enabling time sync...");
        SimModule::enableTimeSync();
        delay(20000); // Ждём NITZ-обновление от вышки
        if (SimModule::syncSystemClock()) {
            Serial.println("✓ Time ready");
            blink(2, 1000);
        }
    }
    printCurrentTime();
}

void SIM_check_signal() {
    Serial.println(">>> Action 1 (GPIO 8)");
    blink(1, 1500);
    blink(1, 750);

    uint8_t chanel = readSwitchState();
    // Обрабатываем только реальные изменения (не 0xFF)
    if (chanel != 0xFF) {
        Serial.printf("Switch changed: %d%d (dec: %d)\n",
                      (chanel & 0x02) ? 1 : 0, // Ползунок 2
                      (chanel & 0x01) ? 1 : 0, // Ползунок 1
                      chanel);
    }
    if (sim_activate(true)) {
        int signal = SimModule::getSignalQuality();
        if (signal <= 20) {
            Serial.printf("signal %i, 1", signal);
            blink(1, 750);
        } else if (signal <= 40) {
            Serial.printf("signal %i, 2", signal);
            blink(2, 750);
        } else if (signal <= 60) {
            Serial.printf("signal %i, 3", signal);
            blink(3, 750);
        } else if (signal <= 80) {
            Serial.printf("signal %i, 4", signal);
            blink(4, 750);
        } else {
            Serial.printf("signal %i, 5", signal);
            blink(5, 750);
        }
        if (!isTime()) {
            getNetTime();
        }
    } else {
        blink(10, 250);
    }
}

void mqtt_send() {
    yield();
    if (SimModule::mqttConnect()) {
        int l = stack.count();
        Serial.printf("всего пакетов %i\n", l);
        for (int i = 0; i < l; i++) {
            if (stack.pop(g_packet)) {
                Serial.printf("   ✅ Popped packet #%d\n", i);
                g_packet[11] = (byte)rssi1;
                printHEX(g_packet, (int)g_packet[0]);
                if (!SimModule::mqttSendPacket(g_packet, (int)g_packet[0])) {
                    g_packet[11] = 0;
                    stack.push(g_packet);
                } else {
                    blink(1, 500);
                }
            } else {
                Serial.printf("   ❌ Failed to pop packet #%d (stack empty?)\n",
                              i);
            }
        }
        yield();
        SimModule::mqttdisconnect();
    } else {
        blink(10, 250);
    }
}

#endif

bool lora_activate(bool act) {
    if (act) {
        enable_lora(1);
        delay(1000);
        return LoRa::begin();
    } else {
        bool state1 = LoRa::end();
        delay(1000);
        return state1;
    }
}

void lora_send(int l = 0) {
    yield();
    bool da = false;
    if (lora_activate(true)) {
        if (l == 0) {
            l = stack.count();
        }
        Serial.printf("всего пакетов %i\n", l);
        for (int i = 0; i < l; i++) {
            if (stack.pop(g_packet)) {
                Serial.printf("   ✅ Popped packet #%d\n", i);
                g_packet[11] = (byte)rssi1;
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
                                    byte dateBytes[6] = {
                                        rxBuffer[4], rxBuffer[5], rxBuffer[6],
                                        rxBuffer[7], rxBuffer[8], rxBuffer[9]};
                                    if(setTimeFromHexBytes(dateBytes))
                                    {printCurrentTime();}
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
                Serial.printf("   ❌ Failed to pop packet #%d (stack empty?)\n",
                              i);
            }
            if (!da) {
                Serial.printf("   ❌ Failed sending");
                stack.push(g_packet);
                blink(10, 250);
            }
        }

        yield();
        lora_activate(false);
    } else {
        blink(10, 250);
    }
}

int lora_rssi(byte *pac) {
    int len = 0;
    if (LoRa::send(pac, pac[0])) {
        for (int i = 0; i < 100; i++) {
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
                        Serial.printf("rssi get %f,perc %i", rssi, perc);
                        byte dateBytes[6] = {rxBuffer[4], rxBuffer[5],
                                             rxBuffer[6], rxBuffer[7],
                                             rxBuffer[8], rxBuffer[9]};
                        setTimeFromHexBytes(dateBytes);
                        printCurrentTime();
                        blink(1, 2000);
                    }
                }
                return perc;
            } else {
                Serial.print('.');
            }
            delay(50);
        }

    } else {
        Serial.printf("   ❌ Failed Send)\n");
        return 0;
    }
}

void lora_check_signal() {
    Serial.println(">>> Action 1 (GPIO 8)");
    blink(1, 1500);
    blink(1, 750);
    byte pac[13] = {};

    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    Serial.println("da");
    size_t len = preparePacket(pac, 13, ID, Battery, dateBytes, 1, 1);
    pac[0] = (byte)(len);

    lora_activate(true);
    if (LoRa::configSet(17, 1)) {
        LoRa::configGet();
    }
    int rssi = lora_rssi(pac);
    if (rssi <= 20) {
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

#if BOARD_TYPE == 0 and NET == 1
void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 250; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
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
    sleep(120);
    // sleep(TIME_TO_SLEEP);
}
void loop() {}
#elif BOARD_TYPE == 0 and NET == 0
void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 50; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
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
    // byte newTime[6] = {24, 4, 29, 15, 30, 45};

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
        dataPrepare();
        if (lora_activate(true)) {
            lora_send();
        }
        Serial.println("            complited");
        Serial.println("one run");
        break;
    }
    sleep(30);
    // sleep(TIME_TO_SLEEP);
}
void loop() {}
#elif BOARD_TYPE == 1 and BOARD_REV == 3

void setup() {
    initPins();
    Serial.begin(115200); // монитор порта
    for (int i = 0; i < 250; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    Serial.printf("Rev: %d, NET: %d/%d\n", BOARD_REV, NET);
    // enable_power(1);
    enable_lora(1);
    // enable_sim(1);
    // getNetTime();
    byte newTime[6] = {24, 4, 29, 15, 30, 45};
    setTimeFromHexBytes(newTime);
    lora_activate(true);
    // if (LoRa::configSet(17, 1)) {
    //     LoRa::configGet();
    // }
}

void loop() {

    // 🔹 Неблокирующий опрос: проверяем и читаем за один вызов
    int len = LoRa::receivePacketNB(rxBuffer, sizeof(rxBuffer));

    if (len > 0) {
        digitalWrite(LED_PIN, HIGH);
        // ✅ Пакет получен — обрабатываем
        Serial.printf("📨 Packet %d bytes\n", len);
        printHEX(rxBuffer, len);
        Serial.println(len);
        byte date[6] = {};
        getPackedTimeBytes(date);
        byte pac[] = {rxBuffer[1], rxBuffer[2], rxBuffer[3], 0xff,    date[0],
                      date[1],     date[2],     date[3],     date[4], date[5]};
        printHEX(pac, 10);
        LoRa::send(pac, 10);
        delay(200);
        digitalWrite(LED_PIN, LOW); // Твоя функция обработки

    } else if (len < 1) {
        // Serial.print(".");
    }
    delay(10);
    yield();
}
#endif
