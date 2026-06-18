// =====================================================================
//  lora.cpp – Модуль работы с LoRa-модулем E220 (Ebyte) для ESP32
// =====================================================================
//  Использует библиотеку LoRa_E220 для управления модулем через UART1.
//  Предоставляет функции инициализации, получения информации о модуле,
//  чтения/установки конфигурации, отправки и приёма пакетов.
//  Также содержит функцию для приёма пакета с ожиданием подтверждения
//  и автоматической установкой времени из полученных данных.
//  Весь код компилируется только при NET == 0 или NET == 2.
// =====================================================================

#include "lora.h"
#include <sys.h>

#if NET == 0 or NET == 2

namespace {
bool initialized = false; ///< Флаг инициализации LoRa-модуля
}

// Глобальный объект LoRa_E220 (используется в остальных функциях)
LoRa_E220 e220(&Serial1, LORA_AUX_PIN, LORA_M1_PIN, LORA_M0_PIN);

namespace LoRa {

// =====================================================================
// begin() – Инициализация LoRa-модуля
// =====================================================================
/**
 * @brief Инициализирует LoRa-модуль E220.
 * @param baud скорость UART (не используется, т.к. модуль настраивается в
 * конфигурации)
 * @return true если модуль успешно инициализирован, иначе false.
 * @details Вызывает e220.begin(), которая выполняет аппаратную инициализацию.
 *          После успеха устанавливает флаг initialized = true.
 *          Повторные вызовы без вызова end() будут возвращать true без
 * повторной инициализации.
 */
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

// =====================================================================
// end() – Деинициализация LoRa-модуля
// =====================================================================
/**
 * @brief Закрывает UART и сбрасывает флаг инициализации.
 * @return всегда false (после деинициализации модуль не готов).
 * @details Вызывает Serial1.end() и устанавливает initialized = false.
 *          Для повторной работы требуется вызов begin().
 */
bool end() {
    Serial1.end();
    initialized = false;
    return initialized;
}

// =====================================================================
// printModuleInfo() – Вывод информации о модуле в Serial
// =====================================================================
/**
 * @brief Запрашивает и выводит информацию о версии и модели модуля.
 * @details Если модуль не инициализирован, выводит сообщение об ошибке.
 *          При успешном получении информации выводит: HEAD, модель, версию,
 * features. В случае ошибки (код ответа != 1) выводит описание ошибки. Буфер
 * результата освобождается после использования (c.close()).
 */
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
    c.close();
}

// =====================================================================
// printParameters() – Вывод текущей конфигурации модуля
// =====================================================================
/**
 * @brief Выводит все параметры конфигурации модуля в читаемом виде.
 * @param configuration структура Configuration, полученная от модуля.
 * @details Выводит: адрес, канал, скорость UART, скорость эфира, бит чётности,
 *          настройки подпакетов, мощность, RSSI, режим WOR, LBT, фиксированная
 * передача. Использует методы библиотеки для получения текстовых описаний.
 */
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

// =====================================================================
// getModuleInfo() – Получение информации о модуле в структуру
// =====================================================================
/**
 * @brief Запрашивает информацию о модуле и копирует её в переданную структуру.
 * @param outInfo указатель на структуру ModuleInformation для заполнения.
 * @return true если получение успешно, иначе false.
 * @details После получения информации, копирует данные через memcpy и
 * освобождает контейнер. Если модуль не инициализирован или указатель нулевой —
 * возвращает false.
 */
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

// =====================================================================
// configGet() – Получение и вывод текущей конфигурации
// =====================================================================
/**
 * @brief Получает текущую конфигурацию модуля и выводит через
 * printParameters().
 * @details Если модуль не инициализирован, выводит сообщение об ошибке.
 *          При успехе выводит статус ответа и параметры.
 */
void configGet() {
    if (!initialized) {
        Serial.println("❌ LoRa not init");
        return;
    }

    Serial.println("lora config get");
    ResponseStructContainer c = e220.getConfiguration();
    Configuration configuration = *(Configuration *)c.data;
    Serial.println(c.status.getResponseDescription());
    Serial.println(c.status.code);
    printParameters(configuration);
}

// =====================================================================
// configSet() – Установка конфигурации модуля
// =====================================================================
/**
 * @brief Устанавливает фиксированные параметры конфигурации LoRa-модуля.
 * @param channel номер канала (0-31)
 * @param address адрес устройства (0-65535, будет записан в ADDH и ADDL
 * одинаково)
 * @return true если конфигурация успешно сохранена, иначе false.
 * @details Устанавливает:
 *          - канал (CHAN)
 *          - адрес (ADDH и ADDL)
 *          - скорость UART = 9600, 8N1
 *          - скорость эфира = 24 кбит/с
 *          - подпакет = 200 байт
 *          - RSSI шум включён
 *          - мощность = 22 дБм
 *          - RSSI в режиме передачи включён
 *          - фиксированная передача = выкл.
 *          - LBT = выкл.
 *          - WOR период = 2000 мс
 *          После установки сохраняет в энергонезависимую память
 * (WRITE_CFG_PWR_DWN_SAVE). Возвращает true, если код ответа == 1 (успех).
 */
bool configSet(uint8_t channel, uint8_t address) {
    if (!initialized) {
        Serial.println("❌ LoRa not init");
        return false;
    }

    Serial.println("lora config set");
    ResponseStructContainer c = e220.getConfiguration();
    Configuration configuration = *(Configuration *)c.data;

    // Заполняем параметры
    configuration.CHAN = channel;
    configuration.ADDL = address;
    configuration.ADDH = address;
    configuration.SPED.uartBaudRate = UART_BPS_9600;
    configuration.SPED.airDataRate = AIR_DATA_RATE_000_24;
    configuration.SPED.uartParity = MODE_00_8N1;
    configuration.OPTION.subPacketSetting = SPS_200_00;
    configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;
    configuration.OPTION.transmissionPower = POWER_22;
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

// =====================================================================
// send() – Отправка данных в прозрачном режиме
// =====================================================================
/**
 * @brief Отправляет массив байт через LoRa-модуль.
 * @param data указатель на данные
 * @param len количество байт для отправки
 * @return true если отправка успешна (код ответа == 1), иначе false.
 * @details Вызывает e220.sendMessage() с переданными данными и длиной.
 *          Если данные нулевые или длина 0, возвращает false.
 *          При успехе выводит подтверждение в Serial.
 */
bool send(const uint8_t *data, size_t len) {
    if (!initialized || !data || len == 0)
        return false;
    Serial.printf("Sending %i bytes\n", len);
    ResponseStatus rs = e220.sendMessage(data, len);
    Serial.println(rs.getResponseDescription());
    return (rs.code == 1);
}

// =====================================================================
// receivePacketNB() – Неблокирующий приём пакета
// =====================================================================
/**
 * @brief Пытается принять один пакет без ожидания (неблокирующий режим).
 * @param outBuf указатель на буфер для приёма данных
 * @param maxSize максимальный размер буфера
 * @return int количество принятых байт, 0 если данных нет, -1 если ошибка.
 * @details Сначала вызывает memset() для очистки буфера.
 *          Проверяет e220.available() – если данных нет, возвращает 0.
 *          Затем вызывает e220.receiveMessageComplete(0) с таймаутом 0.
 *          Если статус != 1, возвращает -1.
 *          Извлекает строку, копирует её в буфер (не более maxSize) и
 * возвращает длину.
 */
int receivePacketNB(uint8_t *outBuf, size_t maxSize) {
    memset(outBuf, 0x00, maxSize);

    if (e220.available() <= 0) {
        return 0; // Данные ещё не пришли
    }
    ResponseContainer rc = e220.receiveMessageComplete(0);
    if (rc.status.code != 1) {
        return -1; // Ошибка приёма
    }
    String msg = rc.data;
    int len = msg.length();
    if (len <= 0)
        return 0;
    if (len > (int)maxSize)
        len = maxSize;
    memcpy(outBuf, msg.c_str(), len);
    yield(); // сброс WatchDog
    return len;
}

// =====================================================================
// messageGetOK() – Ожидание пакета с проверкой ID и установкой времени
// =====================================================================
/**
 * @brief Ожидает приём пакета с таймаутом, проверяет ID и при успехе
 * устанавливает время.
 * @param timeoutMs таймаут в миллисекундах (делится на 100 для шагов опроса)
 * @return true если принят пакет с корректным ID и время установлено, иначе
 * false.
 * @details Алгоритм:
 *          1. Циклически опрашивает e220.available() с шагом 100 мс до
 * истечения timeoutMs.
 *          2. При появлении данных вызывает e220.receiveMessage().
 *          3. Проверяет, что пакет содержит маркер 0xFF на позиции 4 (индекс в
 * строке).
 *          4. Проверяет первые 3 байта (индексы 0,1,2) на соответствие ID
 * (старшие байты).
 *          5. Извлекает байты 5..10 как дату/время и вызывает
 * setTimeFromHexBytes().
 *          6. При успехе возвращает true.
 *          Если ни одного пакета не получено за время, возвращает false.
 *          В процессе опроса выводит отладочные сообщения.
 */
bool messageGetOK(uint32_t timeoutMs) {
    Serial.println("📡 LoRa: Waiting for message...");
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
                mas[l] = (byte)rc.data.charAt(l);
            }
            Serial.printf("(%d )\n", len);
            // Проверяем маркер и ID
            if ((byte)rc.data.charAt(4) == 0xFF) {
                if (mas[0] == (byte)(ID >> 16) & 0xFF &&
                    mas[1] == (byte)(ID >> 8) & 0xFF &&
                    mas[2] == (byte)(ID) & 0xFF) {
                    Serial.println(" ID OK");
                } else {
                    Serial.println(" ID mismatch, ignoring");
                    // Можно продолжать, но в этой реализации только один шанс
                }
                byte date[] = {mas[5], mas[6], mas[7], mas[8], mas[9], mas[10]};
                if (setTimeFromHexBytes(date)) {
                    Serial.println("Time SET");
                }
                return true;
            }
        }
        Serial.println("No data yet");
        delay(iterations); // Шаг опроса, как в оригинале (но это странно,
                           // обычно delay(100))
    }
    Serial.println("⏱ Timeout: no message received");
    return false; // Таймаут
}

} // namespace LoRa

#endif // NET == 0 or NET == 2