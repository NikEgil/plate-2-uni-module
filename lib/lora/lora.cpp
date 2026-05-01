#include "lora.h"
#include <sys.h>
namespace {
// HardwareSerial *Serial1 = nullptr; // 🔹 HardwareSerial, не SoftwareSerial
// LoRa_E220 e220 = nullptr;

bool initialized = false;
} // namespace
LoRa_E220 e220(&Serial1, LORA_AUX_PIN, LORA_M1_PIN, LORA_M0_PIN);

namespace LoRa {
bool begin(uint32_t baud) {
    if (initialized)
        return true;
    if (e220.begin()) {
        initialized = true;
    } else {
        initialized = false;
    }
    return initialized;
}
bool end() {
    Serial1.end();
    initialized = false;
    return initialized;
}
void printModuleInfo() {
    if (!initialized) {
        Serial.println("❌ LoRa not init");
        return;
    }

    ResponseStructContainer c = e220.getModuleInformation();
    if (c.status.code != 1) {
        Serial.printf("⚠️ Get info failed: %s\n",
                      c.status.getResponseDescription());
        c.close();
        return;
    }

    struct ModuleInformation *info = (struct ModuleInformation *)c.data;

    Serial.println("----------------------------------------");
    Serial.printf("HEAD: 0x%02X 0x%02X LEN:%d\n", info->COMMAND,
                  info->STARTING_ADDRESS, info->LENGHT);
    Serial.printf("Model: 0x%02X (%s)\n", info->model,
                  (info->model == 0xE7) ? "E220-900T22S" : "Unknown");
    Serial.printf("Version: 0x%02X\n", info->version);
    Serial.printf("Features: 0x%02X\n", info->features);
    Serial.println("----------------------------------------");

    c.close(); // 🔹 Обязательно!
}

void printParameters(struct Configuration configuration) {
    Serial.println("----------------------------------------");
    Serial.print(F("HEAD : "));
    Serial.print(configuration.COMMAND, HEX);
    Serial.print(" ");
    Serial.print(configuration.STARTING_ADDRESS, HEX);
    Serial.print(" ");
    Serial.println(configuration.LENGHT, HEX);
    Serial.println(F(" "));
    Serial.print(F("AddH : "));
    Serial.println(configuration.ADDH, HEX);
    Serial.print(F("AddL : "));
    Serial.println(configuration.ADDL, HEX);
    Serial.println(F(" "));
    Serial.print(F("Chan : "));
    Serial.print(configuration.CHAN, DEC);
    Serial.print(" -> ");
    Serial.println(configuration.getChannelDescription());
    Serial.println(F(" "));
    Serial.print(F("SpeedParityBit     : "));
    Serial.print(configuration.SPED.uartParity, BIN);
    Serial.print(" -> ");
    Serial.println(configuration.SPED.getUARTParityDescription());
    Serial.print(F("SpeedUARTDatte     : "));
    Serial.print(configuration.SPED.uartBaudRate, BIN);
    Serial.print(" -> ");
    Serial.println(configuration.SPED.getUARTBaudRateDescription());
    Serial.print(F("SpeedAirDataRate   : "));
    Serial.print(configuration.SPED.airDataRate, BIN);
    Serial.print(" -> ");
    Serial.println(configuration.SPED.getAirDataRateDescription());
    Serial.println(F(" "));
    Serial.print(F("OptionSubPacketSett: "));
    Serial.print(configuration.OPTION.subPacketSetting, BIN);
    Serial.print(" -> ");
    Serial.println(configuration.OPTION.getSubPacketSetting());
    Serial.print(F("OptionTranPower    : "));
    Serial.print(configuration.OPTION.transmissionPower, BIN);
    Serial.print(" -> ");
    Serial.println(configuration.OPTION.getTransmissionPowerDescription());
    Serial.print(F("OptionRSSIAmbientNo: "));
    Serial.print(configuration.OPTION.RSSIAmbientNoise, BIN);
    Serial.print(" -> ");
    Serial.println(configuration.OPTION.getRSSIAmbientNoiseEnable());
    Serial.println(F(" "));
    Serial.print(F("TransModeWORPeriod : "));
    Serial.print(configuration.TRANSMISSION_MODE.WORPeriod, BIN);
    Serial.print(" -> ");
    Serial.println(
        configuration.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
    Serial.print(F("TransModeEnableLBT : "));
    Serial.print(configuration.TRANSMISSION_MODE.enableLBT, BIN);
    Serial.print(" -> ");
    Serial.println(
        configuration.TRANSMISSION_MODE.getLBTEnableByteDescription());
    Serial.print(F("TransModeEnableRSSI: "));
    Serial.print(configuration.TRANSMISSION_MODE.enableRSSI, BIN);
    Serial.print(" -> ");
    Serial.println(
        configuration.TRANSMISSION_MODE.getRSSIEnableByteDescription());
    Serial.print(F("TransModeFixedTrans: "));
    Serial.print(configuration.TRANSMISSION_MODE.fixedTransmission, BIN);
    Serial.print(" -> ");
    Serial.println(
        configuration.TRANSMISSION_MODE.getFixedTransmissionDescription());
    Serial.println("----------------------------------------");
}

bool getModuleInfo(struct ModuleInformation *outInfo) {
    if (!initialized || !outInfo)
        return false;

    ResponseStructContainer c = e220.getModuleInformation();
    if (c.status.code != 1) {
        c.close();
        return false;
    }

    memcpy(outInfo, c.data, sizeof(struct ModuleInformation));
    c.close();
    return true;
}

void configGet() {
    if (!initialized) {
        Serial.println("❌ LoRa not init");
    }

    Serial.println("lora config get");
    ResponseStructContainer c;
    c = e220.getConfiguration();
    // It's important get configuration pointer before all other operation
    Configuration configuration = *(Configuration *)c.data;
    Serial.println(c.status.getResponseDescription());
    Serial.println(c.status.code);
    printParameters(configuration);
}

bool configSet(uint8_t channel, uint8_t address) {
    if (!initialized) {
        Serial.println("❌ LoRa not init");
        return false;
    }

    Serial.println("lora config set");
    ResponseStructContainer c;
    c = e220.getConfiguration();
    Configuration configuration = *(Configuration *)c.data;
    configuration.CHAN = channel;
    configuration.ADDL = address;
    configuration.ADDH = address;
    configuration.SPED.uartBaudRate = UART_BPS_9600;
    configuration.SPED.airDataRate = AIR_DATA_RATE_000_24;
    // AIR_DATA_RATE_010_24
    configuration.SPED.uartParity = MODE_00_8N1;
    configuration.OPTION.subPacketSetting = SPS_200_00;
    configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;
    configuration.OPTION.transmissionPower = POWER_10;
    configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
    configuration.TRANSMISSION_MODE.fixedTransmission = 0;
    configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
    configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;
    ResponseStatus rs =
        e220.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
    Serial.println(rs.getResponseDescription());
    Serial.println(rs.code);

    delay(500);

    return (rs.code == 1);
}

bool send(const uint8_t *data, size_t len) {
    if (!initialized || !data || len == 0)
        return false;
    Serial.printf("Sending %i bytes\n", len);
    // Отправка в режиме прозрачной передачи
    ResponseStatus rs = e220.sendMessage(data, len);
    Serial.println(rs.getResponseDescription());
    return (rs.code);
}

void messageGetOKrssi(int rssi, uint32_t timeoutMs) {

    Serial.println("📡 LoRa: Waiting for message...");

    // Рассчитываем количество итераций (шаг 200 мс, как в оригинале)
    int iterations = timeoutMs / 100;
    if (iterations < 1)
        iterations = 1;

    for (int i = 0; i < iterations; i++) {
        Serial.printf("  [%d/%d] Checking... ", i + 1, iterations);
        if (e220.available() > 1) {
            Serial.println("Data available, receiving...");
            ResponseContainer rc = e220.receiveMessage();

            if (rc.status.code != 1) {
                Serial.printf("⚠️ Receive failed: %s\n",
                              rc.status.getResponseDescription());
                rssi = 0;
            }
            String msg = rc.data;
            int len = msg.length();
            byte mas[len] = {};
            for (int l = 0; l < len; l++) {
                mas[i] = (byte)rc.data.charAt(i);
            }
            Serial.printf("(%d )\n", len);
            if ((byte)rc.data.charAt(4) == 0xFF) {
                if (mas[0] == (byte)(ID >> 16) & 0xFF &
                    mas[1] == (byte)(ID >> 8) & 0xFF & mas[2] == (byte)(ID) &
                    0xFF) {
                    Serial.println(" ID OK");
                }
                byte date[] = {mas[5], mas[6], mas[7], mas[8], mas[9], mas[10]};
                if (setTimeFromHexBytes(date)) {
                    Serial.println("Time SET");
                }
                rssi = (int)mas[len];
                return;
            }
        }

        Serial.println("No data yet");
        delay(iterations); // Шаг опроса, как в оригинале
    }
    Serial.println("⏱ Timeout: no message received");
    rssi = 0; // Таймаут
}

int receivePacket(uint8_t* outBuf, size_t maxSize, uint32_t timeoutMs) {
    Serial.println("📡 LoRa: Waiting for packet...");

    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        
        // Проверяем наличие данных (>1 байта, как в твоём примере)
        if (e220.available() > 1) {
            Serial.println("📥 Data available, receiving...");
            
            // 🔹 3. Приём через библиотеку
            ResponseContainer rc = e220.receiveMessage();
            
            // 🔹 4. Проверка статуса
            if (rc.status.code != 1) {
                Serial.printf("⚠️ Receive failed: %s\n", rc.status.getResponseDescription());
                return -1;
            }
            
            // 🔹 5. Конвертация String → byte[]
            String msg = rc.data;
            int len = msg.length();
            
            // Защита от переполнения буфера
            if (len > (int)maxSize) {
                Serial.printf("⚠️ Packet too large (%d > %d), truncating\n", len, maxSize);
                len = maxSize;
            }
            
            // 🔹 6. Копирование в выходной буфер (исправленная версия твоего цикла)
            for (int i = 0; i < len; i++) {
                outBuf[i] = (uint8_t)msg.charAt(i);
            }
            
            // 🔹 7. Логирование
            Serial.printf("✅ Received %d bytes\n", len);
            return len;  // Успех: возвращаем длину
        }
        delay(50);
        yield();
    }
    Serial.println("⏱ Timeout: no packet received");
    return 0;
}

bool packetAvailable() {
    if (!initialized) return false;
    return (e220.available() > 1);  // >1 байта = полный пакет
}

// 🔹 Неблокирующий приём пакета
int receivePacketNB(uint8_t* outBuf, size_t maxSize) {
    // 1. Базовые проверки
    memset(outBuf,0x00,maxSize);

    // 2. ПРОВЕРКА СТАТУСА (Non-blocking gate)
    // Согласно мануалу (стр. 10), читаем только если данные готовы.
    // Библиотека xreef внутри available() проверяет AUX, но явно лучше.
    if (e220.available() <= 0) {
        return 0; // Данные ещё не пришли или AUX=LOW
    }

    // 3. Чтение сообщения (используем функцию библиотеки)
    // receiveMessageComplete ждет завершения пакета, если он в процессе приема
    ResponseContainer rc = e220.receiveMessageComplete(0);
    // 4. Проверка статуса ответа
    if (rc.status.code != 1) {
        // Ошибка приёма (CRC, таймаут и т.д.)
        #ifdef LORA_DEBUG
            Serial.printf("️ LoRa RX Error: %s\n", rc.status.getResponseDescription());
        #endif
        return -1;
    }

    // 5. Копирование данных из String в byte[]
    String msg = rc.data;
    int len = msg.length();

    if (len <= 0) {
        return 0;
    }

    // Защита от переполнения буфера
    if (len > (int)maxSize) {

        len = maxSize;
    }

    // Копируем байты (String::c_str() возвращает const char*)
    memcpy(outBuf, msg.c_str(), len);

    // 6. Очистка памяти (Критично для ESP32!)
    // 7. Сброс WDT, так как работа со строками может занять время
    yield();

    return len;
}

bool messageGetOK(uint32_t timeoutMs) {

    Serial.println("📡 LoRa: Waiting for message...");

    // Рассчитываем количество итераций (шаг 200 мс, как в оригинале)
    int iterations = timeoutMs / 100;
    if (iterations < 1)
        iterations = 1;

    for (int i = 0; i < iterations; i++) {
        Serial.printf("  [%d/%d] Checking... ", i + 1, iterations);
        if (e220.available() > 1) {
            Serial.println("Data available, receiving...");
            ResponseContainer rc = e220.receiveMessage();

            if (rc.status.code != 1) {
                Serial.printf("⚠️ Receive failed: %s\n",
                              rc.status.getResponseDescription());
                return false;
            }
            String msg = rc.data;
            int len = msg.length();
            byte mas[len] = {};
            for (int l = 0; l < len; l++) {
                mas[i] = (byte)rc.data.charAt(i);
            }
            Serial.printf("(%d )\n", len);
            if ((byte)rc.data.charAt(4) == 0xFF) {
                if (mas[0] == (byte)(ID >> 16) & 0xFF &
                    mas[1] == (byte)(ID >> 8) & 0xFF & mas[2] == (byte)(ID) &
                    0xFF) {
                    Serial.println(" ID OK");
                }
                byte date[] = {mas[5], mas[6], mas[7], mas[8], mas[9], mas[10]};
                if (setTimeFromHexBytes(date)) {
                    Serial.println("Time SET");
                }
                return true;
            }
        }

        Serial.println("No data yet");
        delay(iterations); // Шаг опроса, как в оригинале
    }
    Serial.println("⏱ Timeout: no message received");
    return false; // Таймаут
}
} // namespace LoRa
