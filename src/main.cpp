#include "FlashStack.h"
#include <Arduino.h>
#include <defenitions.h>
#include <rs.h>
#include <sim.h>
#include <sys.h>
FlashStack stack;
uint8_t g_packet[198];
int Battery;

byte tableSens[5] = {0x00, 0x08, 0x00, 0x00, 0x01};
RTC_DATA_ATTR int state = 0;
int rssi1 = 0;
// #define rs485Serial Serial1
// LoRa_E220 e220ttl(&MySerial, 15, 21, 19); //  RXTX AUX M0 M1

// Serial1.begin(115200, SERIAL_8N1, 16, 17);
// LoRa_E220 e220ttl(&Serial1, 8, 18, 21); //  RX TX AUX M0 M1

// void printParameters(struct Configuration configuration)
// {
// 	Serial.println("----------------------------------------");
// 	Serial.print(F("HEAD : "));
// 	Serial.print(configuration.COMMAND, HEX);
// 	Serial.print(" ");
// 	Serial.print(configuration.STARTING_ADDRESS, HEX);
// 	Serial.print(" ");
// 	Serial.println(configuration.LENGHT, HEX);
// 	Serial.println(F(" "));
// 	Serial.print(F("AddH : "));
// 	Serial.println(configuration.ADDH, HEX);
// 	Serial.print(F("AddL : "));
// 	Serial.println(configuration.ADDL, HEX);
// 	Serial.println(F(" "));
// 	Serial.print(F("Chan : "));
// 	Serial.print(configuration.CHAN, DEC);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.getChannelDescription());
// 	Serial.println(F(" "));
// 	Serial.print(F("SpeedParityBit     : "));
// 	Serial.print(configuration.SPED.uartParity, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.SPED.getUARTParityDescription());
// 	Serial.print(F("SpeedUARTDatte     : "));
// 	Serial.print(configuration.SPED.uartBaudRate, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.SPED.getUARTBaudRateDescription());
// 	Serial.print(F("SpeedAirDataRate   : "));
// 	Serial.print(configuration.SPED.airDataRate, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.SPED.getAirDataRateDescription());
// 	Serial.println(F(" "));
// 	Serial.print(F("OptionSubPacketSett: "));
// 	Serial.print(configuration.OPTION.subPacketSetting, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.OPTION.getSubPacketSetting());
// 	Serial.print(F("OptionTranPower    : "));
// 	Serial.print(configuration.OPTION.transmissionPower, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.OPTION.getTransmissionPowerDescription());
// 	Serial.print(F("OptionRSSIAmbientNo: "));
// 	Serial.print(configuration.OPTION.RSSIAmbientNoise, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.OPTION.getRSSIAmbientNoiseEnable());
// 	Serial.println(F(" "));
// 	Serial.print(F("TransModeWORPeriod : "));
// 	Serial.print(configuration.TRANSMISSION_MODE.WORPeriod, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
// 	Serial.print(F("TransModeEnableLBT : "));
// 	Serial.print(configuration.TRANSMISSION_MODE.enableLBT, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getLBTEnableByteDescription());
// 	Serial.print(F("TransModeEnableRSSI: "));
// 	Serial.print(configuration.TRANSMISSION_MODE.enableRSSI, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getRSSIEnableByteDescription());
// 	Serial.print(F("TransModeFixedTrans: "));
// 	Serial.print(configuration.TRANSMISSION_MODE.fixedTransmission, BIN);
// 	Serial.print(" -> ");
// 	Serial.println(configuration.TRANSMISSION_MODE.getFixedTransmissionDescription());
// 	Serial.println("----------------------------------------");
// }
// void printModuleInformation(struct ModuleInformation moduleInformation)
// {
// 	Serial.println("----------------------------------------");
// 	Serial.print(F("HEAD: "));
// 	Serial.print(moduleInformation.COMMAND, HEX);
// 	Serial.print(" ");
// 	Serial.print(moduleInformation.STARTING_ADDRESS, HEX);
// 	Serial.print(" ");
// 	Serial.println(moduleInformation.LENGHT, DEC);
// 	Serial.print(F("Model no.: "));
// 	Serial.println(moduleInformation.model, HEX);
// 	Serial.print(F("Version  : "));
// 	Serial.println(moduleInformation.version, HEX);
// 	Serial.print(F("Features : "));
// 	Serial.println(moduleInformation.features, HEX);
// 	Serial.println("----------------------------------------");
// }

// void LORA_config_get()
// {
// 	Serial.println("lora config get");
// 	ResponseStructContainer c;
// 	c = e220ttl.getConfiguration();
// 	// It's important get configuration pointer before all other operation
// 	Configuration configuration = *(Configuration *)c.data;
// 	Serial.println(c.status.getResponseDescription());
// 	Serial.println(c.status.code);
// 	printParameters(configuration);
// }

// void LORA_config_set(int chanel, byte add)
// {
// 	Serial.println("lora config set");
// 	ResponseStructContainer c;
// 	c = e220ttl.getConfiguration();
// 	Configuration configuration = *(Configuration *)c.data;
// 	configuration.CHAN = chanel;
// 	configuration.ADDL = add;
// 	configuration.ADDH = add;
// 	configuration.SPED.uartBaudRate = UART_BPS_9600;
// 	configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
// 	// AIR_DATA_RATE_010_24
// 	configuration.SPED.uartParity = MODE_00_8N1;
// 	configuration.OPTION.subPacketSetting = SPS_200_00;
// 	configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;
// 	configuration.OPTION.transmissionPower = POWER_10;
// 	configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
// 	configuration.TRANSMISSION_MODE.fixedTransmission = 0;
// 	configuration.TRANSMISSION_MODE.enableLBT = LBT_ENABLED;
// 	configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;
// 	ResponseStatus rs = e220ttl.setConfiguration(configuration,
// WRITE_CFG_PWR_DWN_SAVE); 	Serial.println(rs.getResponseDescription());
// 	Serial.println(rs.code);
// }

// bool LORA_messedge_send(byte datatosend[], int lendatatosend)
// {
// 	LORA_activate(true);
// 	ResponseStatus s;
// 	s = e220ttl.sendMessage(datatosend, lendatatosend);
// 	Serial.println(s.code);
// 	Serial.println(s.getResponseDescription());
// 	LORA_activate(false);
// }

// bool LORA_messedge_get()
// {
// 	Serial.println("wait mes");
// 	for (int i = 0; i < 40; i++)
// 	{
// 		Serial.println(i);
// 		delay(200);
// 		if (e220ttl.available() > 1)
// 		{
// 			ResponseContainer rc = e220ttl.receiveMessage();
// 			if (rc.status.code != 1)
// 			{
// 				Serial.println(rc.status.getResponseDescription());
// 			}
// 			else
// 			{
// 				Serial.println(rc.status.getResponseDescription());
// 				byte newID[3];
// 				byte myID[3] = {};
// 				int length = rc.data.length();
// 				myID[0] = (byte)(ID >> 16) & 0xFF;
// 				myID[1] = (byte)(ID >> 8) & 0xFF;
// 				myID[2] = (byte)(ID) & 0xFF;
// 				// Преобразуем строку в массив байтов
// 				for (int i = 0; i < 3; i++)
// 				{
// 					newID[i] = (byte)rc.data.charAt(i);
// 				}
// 				printHEX(newID, 3);
// 				if (myID[3] == newID[3])
// 				{
// 					Serial.println(" OK Recive");
// 					return true;
// 				}
// 			}
// 		}
// 	}
// 	return false;
// }

// bool LORA_messedge_check()
// {
// 	byte pak[3] = {};
// 	pak[0] = (byte)(ID >> 16) & 0xFF;
// 	pak[1] = (byte)(ID >> 8) & 0xFF;
// 	pak[2] = (byte)(ID) & 0xFF;
// 	pak[3] = 0x22;
// 	LORA_activate(true);
// 	ResponseStatus s;
// 	s = e220ttl.sendMessage(pak, 4);
// 	Serial.println(s.code);
// 	Serial.println(s.getResponseDescription());
// 	if (LORA_messedge_get())
// 	{
// 		Serial.println("OK");
// 		return true;
// 	}
// 	else
// 	{
// 		Serial.println("not OK");
// 		return false;
// 	}
// }

// void sender()
// {
// 	LORA_activate(true);
// 	delay(200);
// 	ResponseStatus s;
// 	byte combinedData[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
// 0x11}; 	for (int i = 0; i < 10; i++)
// 	{
// 		s = e220ttl.sendMessage(combinedData, 10);
// 		Serial.println(s.code);
// 		Serial.println(s.getResponseDescription());
// 		delay(5000);
// 	}
// 	delay(5000);
// }

void mqtt_send() {
    yield();
    if (SimModule::mqttConnect()) {
        int l = stack.count();
        Serial.printf("всего пакетов %i\n", l);
        for (int i = 0; i < 2; i++) {
            if (stack.pop(g_packet)) {
                Serial.printf("   ✅ Popped packet #%d\n", i);
                printHEX(g_packet, (int)g_packet[0]);
                if (!SimModule::mqttSendPacket(g_packet, (int)g_packet[0])) {
                    stack.push(g_packet);
                }

            } else {
                Serial.printf("   ❌ Failed to pop packet #%d (stack empty?)\n",
                              i);
            }
        }
        yield();
        SimModule::mqttdisconnect();
    }
}
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

// void polling(int sens, int lenreg, uint8_t *buf) {
//     Serial.printf("tableSens number: %i,  sensReg number:	%i\n", sens,
//                   lenreg);
//     byte req[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x00};
//     req[0] = (byte)sens;
//     req[5] = (byte)lenreg;
//     byte request[8];
//     int lenresponse = 5 + lenreg * 2;
//     byte response[lenresponse];
//     addCRC(req, 6, request);
//     Serial.println("        reQuest:");
//     printHEX(request, 8);
//     RsModbus::sendData(request, 8);
//     delay(500);
//     RsModbus::receiveData(response, lenresponse, 10);
//     Serial.println("        reSpons:");
//     printHEX(response, lenresponse);
//     memcpy(buf, response, lenresponse);
// }

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
    delay(50);

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

    // Приём: передаём внешний буфер и его размер
    int received = RsModbus::receiveData(outBuf, outBufSize, 150);

    if (received > 0) {
        Serial.printf("  RX[%d]: ", received - 1);
        printHEX(outBuf, received);
    }
    return received; // -1 = ошибка, >=0 = кол-во байт
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

    if (sens == 0) {
        Serial.println("Sensor disabled");
        goto cleanup;
    }

    delay(sensTime[sens] * 1000);

    if (sens == 1) {
        int singleLen = 5 + lenreg * 2;
        res.length = singleLen * 5;

        // Выделяем память в куче
        res.data = (uint8_t *)malloc(res.length);
        if (!res.data) {
            Serial.println("❌ malloc failed (sens==1)");
            goto cleanup;
        }
        memset(res.data, 0x00, res.length);

        // Временный буфер для polling (чуть больше ожидаемого)
        byte tempBuf[singleLen + 16];

        for (int i = 0; i < 5; i++) {
            int received = polling(i + 1, lenreg, tempBuf, sizeof(tempBuf));
            if (received == singleLen && checkCRC(tempBuf, received)) {
                memcpy(res.data + i * singleLen, tempBuf, singleLen);
            } else {
                Serial.printf("⚠️ Addr %d failed\n", i + 1);
            }
        }
        printHEX(res.data, res.length);

    } else {
        res.length = 5 + lenreg * 2;
        res.data = (uint8_t *)malloc(res.length);
        if (!res.data) {
            Serial.println("❌ malloc failed (single)");
            goto cleanup;
        }

        int received = polling(sens, lenreg, res.data, res.length);
        if (received == (int)res.length && checkCRC(res.data, received)) {
            printHEX(res.data, received);
        } else {
            Serial.println("⚠️ Single sensor failed");
        }
    }

    res.valid = true; // Данные собраны (даже если часть ячеек нулевая)

cleanup:
    enable_sens(0);
    Serial.println("✅ Measured");
    digitalWrite(LED_PIN, LOW);
    return res;
}

// void measure(int port) {
//     digitalWrite(LED_PIN, HIGH);
//     enable_power(true);
//     Serial.printf("			Measure, port	%i\n", port);
//     enable_sens(port);
//     delay(1000);
//     if (port == 1) {
//         RsModbus::setChannel(RsModbus::RS_CH1, true);
//         Serial.println("Channel 1 activated");
//     } else {
//         RsModbus::setChannel(RsModbus::RS_CH2, true);
//         Serial.println("Channel 2 activated");
//     }
//     int sens = (int)tableSens[port];
//     int lenreg = sensReg[sens];
//     if (sens == 0) {
//         Serial.println("");
//         return;
//     }
//     delay(sensTime[sens] * 1000);
//     if (sens == 1) {
//         int s = 0;
//         int l = 5 + lenreg * 2;
//         byte all[l * 5];
//         memset(all, 0x00, l * 5);
//         byte response[l];
//         for (int i = 0; i < 5; i++) {
//             memset(response, 0x00, l);
//             polling(i + 1, lenreg, response);
//             if (checkCRC(response,l))
//             {memcpy(all + i * l, response, l);}
//         }
//         printHEX(all, l * 5);
//     } else {
//         byte response[5 + lenreg * 2];
//         polling(sens, lenreg, response);
//     }
//     Serial.println("measured");
// }

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
    //     for (int i = 0; i < 40; i++) {
    //         delay(500);
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

    if (response[0] != 0x00) {
        return true;
    } else {
        return false;
    }
}

bool sim_activate(bool act) {
    if (SimModule::isConnection()) {
        return true;
    }
    if (act) {
        enable_power(true);
        enable_sim(true);
        enable_sens(0);
        SimModule::begin();
        SimModule::activate(true);
        if (SimModule::connect(apn, gprsUser, gprsPass)) {
            Serial.println("	sim connected");
            rssi1 = SimModule::getSignalQuality();
            if (!isTime()) {
                if (SimModule::syncSystemClock()) {
                    Serial.println("✓ Time ready");
                }
                printCurrentTime();
            }

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
        // 	delay(5000);
        // 	Serial.println("Enabling time sync...");
        // 	SimModule::enableTimeSync();
        delay(20000); // Ждём NITZ-обновление от вышки
        if (SimModule::syncSystemClock()) {
            Serial.println("✓ Time ready");
        }
    }
    printCurrentTime();
}

// void dataPrepare() {
//     byte data[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
//     if (isTime()) {
//         getPackedTimeBytes(data);
//     }
//     uint8_t packet[200];
//     size_t len = preparePacket(packet, ID, Battery, data, rssi1, 0);
//     printHEX(packet, 200);
// }

void doAction1() {
    Serial.println(">>> Action 1 (GPIO 8)");
    blink(1, 500);
    blink(1, 250);

    uint8_t chanel = readSwitchState();
    // Обрабатываем только реальные изменения (не 0xFF)
    if (chanel != 0xFF) {
        Serial.printf("Switch changed: %d%d (dec: %d)\n",
                      (chanel & 0x02) ? 1 : 0, // Ползунок 2
                      (chanel & 0x01) ? 1 : 0, // Ползунок 1
                      chanel);
    }

    sim_activate(true);
    getNetTime();
}

void doAction2() {
    Serial.println(">>> Action 2 (GPIO 9)");
    blink(1, 500);
    blink(2, 250);

    searchSensors(1);
    searchSensors(4);
}
void dataPrepare() {
    byte dateBytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (isTime()) {
        getPackedTimeBytes(dateBytes);
    }
    uint8_t *packet = g_packet;
    memset(packet, 0, sizeof(g_packet));
    // 1. Заполняем заголовок и базовые поля
    size_t len = preparePacket(packet, ID, Battery, dateBytes, rssi1, 0);

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
            packet[len] = activeport[port];
            len += 1;

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

void setup() {
    initPins();
    Serial.begin(115200); // монитор портаF
    for (int i = 0; i < 200; i++) {
        if (!Serial) {
            blink(1, 50);
        }
    }
    blink(1, 2000);
    Battery = readBatteryVoltage();
    RsModbus::init(REDE);
    delay(4000);
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
        doAction1();
        break;
    case 2:
        doAction2();
        break;
    default:

        // stack.clear();
        dataPrepare();
        if (sim_activate(true)) {
            Serial.println("coneceted do mqtt");
            mqtt_send();
        }
        // doAction2();
        Serial.println("            complited");
        break;
    }

    sleep(10);

    // printCurrentTime();

    // delay(200);
    // if (connect())
    // {
    // 	mqtt.setServer(broker, 1883);
    // 	if (mqttConnect())
    // 	{
    // 		Serial.println("podklucheno");
    // 		mqtt_send();
    // 	}
    // 	httpRequest(broker, 1883, "/");
    // }
}

void loop() {
    // uint8_t packet[198];
    // memset(packet, 0, 198);
    // packet[0] = 0xAA;
    // packet[1] = millis() & 0xFF;

    // if (stack.push(packet)) {
    //     Serial.printf("✅ Pushed (count: %d)\n", stack.count());
    // } else {
    //     Serial.println("❌ Push failed");
    // }
    // if (stack.count() == 3) {
    //     int l = stack.count();
    //     for (int i = 0; i < l; i++) {
    //         uint8_t packet[198];

    //         if (stack.pop(packet)) {
    //             Serial.printf("   ✅ Popped packet #%d\n", i);
    //             printHEX(packet, 200);
    //             Serial.printf("      Remaining: %d records\n",
    //             stack.count());
    //         } else {
    //             Serial.printf("   ❌ Failed to pop packet #%d (stack
    //             empty?)\n",
    //                           i);
    //         }
    //     }
    //     stack.clear();
    // }
    // delay(2000); // Даем время на сброс WDT между итерациями
}
