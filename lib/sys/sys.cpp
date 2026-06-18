#include <stdint.h>
#include <sys.h>

// Глобальные объекты
esp_adc_cal_characteristics_t adc_chars; ///< Калибровочные характеристики АЦП
Preferences preferences;                 ///< Доступ к энергонезависимой памяти

// Внутренние флаги состояния (сохраняются между вызовами)
namespace {
bool isPowered = false;    ///< Текущее состояние основного питания
int isPortEnable = 0;      ///< Номер активного порта датчиков (0 = все выкл)
bool isSimEnable = false;  ///< Состояние питания SIM
bool isLoraEnable = false; ///< Состояние питания LoRa
} // namespace

// =====================================================================
// Инициализация пинов в зависимости от BOARD_REV и BOARD_TYPE
// =====================================================================

#if BOARD_REV == 3 && BOARD_TYPE == 0
/**
 * @brief Инициализация пинов для платы rev.3, тип 0.
 * @details Настраиваются все линии управления: EG1-EG4, ESIM, EP, ELORA.
 *          Кнопки не используются (закомментированы). АЦП настраивается на 13
 * бит, 6 дБ ослабления и калибруется по опорному напряжению 3300 мВ.
 *          Переключатели SW1/SW2 настроены как INPUT_PULLUP.
 *          Пробуждение по внешним пинам отключено.
 */
void initPins() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(EG1, OUTPUT);
    pinMode(EG2, OUTPUT);
    pinMode(EG3, OUTPUT);
    pinMode(EG4, OUTPUT);
    pinMode(ESIM, OUTPUT);
    pinMode(EP, OUTPUT);
    pinMode(ELORA, OUTPUT);
    pinMode(ADC, INPUT);
    analogReadResolution(13);
    analogSetAttenuation(ADC_6db);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_13, 3300,
                             &adc_chars);
    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
    // esp_sleep_enable_ext1_wakeup(...) – закомментировано
}

#elif BOARD_REV == 3 && BOARD_TYPE == 2
/**
 * @brief Инициализация пинов для платы rev.3, тип 2 (с кнопками).
 * @details Дополнительно к предыдущей конфигурации настраиваются BUT1 и BUT2
 *          как INPUT_PULLUP, и активируется пробуждение по любому из них (LOW).
 */
void initPins() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(EG1, OUTPUT);
    pinMode(EG2, OUTPUT);
    pinMode(EG3, OUTPUT);
    pinMode(EG4, OUTPUT);
    pinMode(ESIM, OUTPUT);
    pinMode(EP, OUTPUT);
    pinMode(ELORA, OUTPUT);
    pinMode(ADC, INPUT);
    analogReadResolution(13);
    analogSetAttenuation(ADC_6db);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_13, 3300,
                             &adc_chars);
    pinMode(BUT1, INPUT_PULLUP);
    pinMode(BUT2, INPUT_PULLUP);
    uint64_t btnMask = (1ULL << BUT1) | (1ULL << BUT2);
    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
    esp_sleep_enable_ext1_wakeup(btnMask, ESP_EXT1_WAKEUP_ANY_LOW);
}

#elif BOARD_REV == 3 && BOARD_TYPE == 1
/**
 * @brief Инициализация пинов для платы rev.3, тип 1 (упрощённая).
 * @details Только основные линии: LED, ESIM, EP, ELORA, SIM_PWR, ADC.
 *          Кнопки и пробуждение по ним не используются.
 */
void initPins() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(ESIM, OUTPUT);
    pinMode(EP, OUTPUT);
    pinMode(ELORA, OUTPUT);
    pinMode(SIM_PWR, OUTPUT);
    pinMode(ADC, INPUT);
    analogReadResolution(13);
    analogSetAttenuation(ADC_6db);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_13, 3300,
                             &adc_chars);
    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
}

#elif BOARD_REV == 1 && BOARD_TYPE == 0
/**
 * @brief Инициализация пинов для платы rev.1, тип 0.
 * @details Старая версия: только LED, EP, EG1-EG4, ADC. Кнопки отсутствуют.
 */
void initPins() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(EP, OUTPUT);
    pinMode(EG1, OUTPUT);
    pinMode(EG2, OUTPUT);
    pinMode(EG3, OUTPUT);
    pinMode(EG4, OUTPUT);
    pinMode(ADC, INPUT);
    analogReadResolution(13);
    analogSetAttenuation(ADC_6db);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_13, 3300,
                             &adc_chars);
    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
}
#endif

// =====================================================================
// Устаревшая функция преобразования RSSI (оставлена для обратной совместимости)
// =====================================================================

/**
 * @brief Преобразует байт RSSI в проценты по старой формуле.
 * @param rssi байт от Ebyte
 * @return byte процент (0…100)
 * @deprecated Используйте rssiToPercent().
 */
byte rssip(byte rssi) {
    float rssi1 = (float)rssi;
    int16_t dbm = (int)-(rssi1 / 2);
    int perc = (uint8_t)(((dbm + 128) * 100) / 108);
    Serial.printf("rssi get %f,perc %i\n", rssi1, perc);
    return (byte)perc;
}

// =====================================================================
// Чтение состояния переключателей (только если NET != 1)
// =====================================================================

#if NET != 1
/**
 * @brief Считывает состояние SW1 и SW2 с аппаратным антидребезгом.
 * @return uint8_t битовая маска: бит0 = SW1 (LOW=вкл), бит1 = SW2.
 * @details Используется статический фильтр: изменение фиксируется только после
 *          того, как состояние остаётся стабильным в течение
 * SWITCH_DEBOUNCE_MS. Это предотвращает ложные срабатывания от дребезга
 * контактов.
 */
uint8_t readSwitchState() {
    static uint8_t lastRaw = 0;
    static unsigned long lastChange = 0;

    // Чтение текущего состояния (LOW = включено, потому что подтяжка к питанию)
    uint8_t raw = 0;
    if (digitalRead(SW1_PIN) == LOW)
        raw |= 0x01;
    if (digitalRead(SW2_PIN) == LOW)
        raw |= 0x02;

    // Если состояние изменилось – сбрасываем таймер стабильности
    if (raw != lastRaw) {
        lastRaw = raw;
        lastChange = millis();
    }

    // Возвращаем новое состояние только после того, как оно продержалось
    // достаточно долго
    if (millis() - lastChange > SWITCH_DEBOUNCE_MS) {
        return raw;
    }
    return raw; // Возвращаем текущее значение, но оно может быть нестабильным –
                // вызывающий должен игнорировать до стабилизации
}
#endif

// =====================================================================
// Обработка кнопок (зависит от BOARD_REV)
// =====================================================================

#if BOARD_REV == 3
/**
 * @brief Определяет причину пробуждения и возвращает код кнопки (для rev.3).
 * @return uint8_t:
 *         - 3 – холодный старт (подача питания или сброс)
 *         - 0 – пробуждение по таймеру или иное
 *         - 1/2 – (закомментировано) пробуждение по BUT1/BUT2
 * @note Реализация для BUT1/BUT2 отключена, так как на большинстве версий плат
 * они не используются.
 */
uint8_t checkButton() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        return 3; // Холодный старт
    }
    // Обработка кнопок через EXT1 – закомментирована
    // if (cause == ESP_SLEEP_WAKEUP_EXT1) { ... }

    return 0; // Таймер или другое
}

/**
 * @brief Ожидает отпускания кнопок (защита от удержания).
 * @details Функция вызывается после пробуждения по кнопке, чтобы избежать
 *          повторного срабатывания, если кнопка ещё нажата. Реализована как
 *          простая задержка с опросом (закомментирована).
 */
void waitForButtonRelease() {
    Serial.println("Waiting for release...");
    for (int i = 0; i < 20; i++) {
        // if (digitalRead(BUT1) == LOW || digitalRead(BUT2) == LOW) return;
        delay(10);
    }
}

#elif BOARD_REV == 1
/**
 * @brief Определяет причину пробуждения для платы rev.1 (без кнопок).
 * @return 3 при холодном старте, иначе 0.
 */
uint8_t checkButton() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED)
        return 3;
    return 0;
}
#endif

// =====================================================================
// Управление сном
// =====================================================================

/**
 * @brief Переводит ESP32 в глубокий сон на заданное количество секунд.
 * @param time время сна (сек)
 * @note Перед засыпанием выводится сообщение в Serial, затем даётся 500 мс
 *       на отправку данных, после чего вызывается esp_deep_sleep_start().
 */
void sleep(int time) {
    esp_sleep_enable_timer_wakeup(time * uS_TO_S_FACTOR);
    Serial.printf("                 Sleep %i sec", time);
    Serial.flush();
    delay(500);
    esp_deep_sleep_start();
}

// =====================================================================
// Измерение напряжения батареи
// =====================================================================

/**
 * @brief Измеряет напряжение батареи через АЦП с калибровкой и усреднением.
 * @return int напряжение в децивольтах (например, 126 → 12.6 В).
 * @details Выполняется 10 последовательных измерений на пине ADC, затем
 *          вычисляется среднее сырое значение. Используя калибровочные точки
 *          CAL_LOW и CAL_HIGH (определены в defenitions.h), строится линейная
 *          зависимость V = k*raw + b, и вычисляется итоговое напряжение.
 *          Результат выводится в Serial и возвращается как целое число
 * децивольт.
 */
int readBatteryVoltage() {
    uint32_t raw = 0;
    for (int i = 0; i < 10; i++) {
        raw += analogReadRaw(ADC);
        delay(20);
    }
    raw /= 10;

    // Линейная интерполяция по двум калибровочным точкам
    float k = (CAL_HIGH.vbat - CAL_LOW.vbat) / (CAL_HIGH.raw - CAL_LOW.raw);
    float b = CAL_LOW.vbat - k * CAL_LOW.raw;
    float v = k * raw + b;
    int vbat = (int)(v * 10);
    Serial.printf("RAW: %i, BAT: %.3f V, int: %i \n", raw, v, vbat);
    return vbat;
}

// =====================================================================
// Светодиодная индикация
// =====================================================================

/**
 * @brief Мигает встроенным светодиодом заданное число раз.
 * @param count количество миганий
 * @param delayy длительность каждого состояния (мс)
 */
void blink(int count, int delayy) {
    for (int i = 0; i < count; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayy);
        digitalWrite(LED_PIN, LOW);
        delay(delayy);
    }
}

// =====================================================================
// Вывод данных в HEX-формате
// =====================================================================

/**
 * @brief Печатает массив байтов в шестнадцатеричном виде через Serial.
 * @param data массив
 * @param len количество байт для вывода
 * @note Для каждого байта добавляется ведущий ноль, если значение < 0x10.
 */
void printHEX(byte data[], int len) {
    if (len == 0) {
        Serial.println("NONE HEX");
        return;
    }
    for (int j = 0; j < len; j++) {
        if (data[j] < 0x10)
            Serial.print("0");
        Serial.print(data[j], HEX);
        if (j != len - 1)
            Serial.print(" ");
    }
    Serial.println();
}

// =====================================================================
// Сохранение и загрузка массива в Flash (Preferences)
// =====================================================================

/**
 * @brief Сохраняет массив из 8 байт в энергонезависимую память.
 * @param data массив (первые 8 байт будут записаны)
 * @note Пространство имён "my-data", ключ "array". После записи выводится
 *       содержимое первых 5 байт для контроля.
 */
void saveArrayToFlash(byte data[]) {
    preferences.begin("my-data", false);
    preferences.putBytes("array", data, 8);
    preferences.end();
    Serial.print("tableSens:		");
    printHEX(data, 5);
}

/**
 * @brief Загружает массив из 8 байт из Flash.
 * @param data выходной массив (должен быть размером ≥ 8)
 * @return true если данные найдены и загружены, иначе false.
 * @note Если данных нет, массив остаётся неизменным, возвращается false.
 */
bool loadArrayFromFlash(byte data[]) {
    preferences.begin("my-data", true);
    size_t len = preferences.getBytes("array", data, 8);
    preferences.end();

    if (len == 8) {
        Serial.println("Данные успешно загружены из памяти");
        printHEX(data, 5);
        return true;
    } else {
        Serial.println("Данные не найдены, инициализируем дефолтные значения");
        return false;
    }
}

/**
 * @brief Преобразует массив байтов в строку HEX (некорректная реализация).
 * @param byteArray данные
 * @param length длина
 * @param str строка, в которую дописывается результат (передаётся по значению!)
 * @deprecated Функция не работает из-за передачи str по значению. Используйте
 *             ручное формирование строки или передавайте по ссылке.
 */
void byteArrayToHexString(const byte *byteArray, int length, String str) {
    for (int i = 0; i < length; i++) {
        if (i > 0)
            str += " ";
        str += String(byteArray[i], HEX);
    }
}

// =====================================================================
// Управление основным питанием (зависит от BOARD_REV)
// =====================================================================

#if BOARD_REV == 2
/**
 * @brief Включает/выключает питание для платы rev.2 (две линии 5В и 12В).
 * @param act true = включить, false = выключить.
 * @details Сначала включается 5В, затем через 400 мс – 12В. При выключении –
 *          аналогично в обратном порядке. Проверяется текущее состояние,
 *          чтобы избежать повторных команд.
 */
void enable_power(bool act) {
    if (act == isPowered)
        return;
    if (act) {
        digitalWrite(E5V, HIGH);
        delay(400);
        digitalWrite(E12V, HIGH);
        delay(400);
        Serial.println("POWER ON");
        isPowered = true;
    } else {
        digitalWrite(E5V, LOW);
        delay(400);
        digitalWrite(E12V, LOW);
        delay(400);
        Serial.println("POWER OFF");
        isPowered = false;
    }
}

#elif BOARD_REV == 3
/**
 * @brief Управление питанием для платы rev.3 (один сигнал EP).
 * @param act true = включить (HIGH), false = выключить (LOW).
 */
void enable_power(bool act) {
    if (act == isPowered)
        return;
    if (act) {
        digitalWrite(EP, HIGH);
        delay(1000);
        Serial.println("POWER ON");
        isPowered = true;
    } else {
        digitalWrite(EP, LOW);
        delay(400);
        Serial.println("POWER OFF");
        isPowered = false;
    }
}
#endif

// =====================================================================
// Управление портами датчиков (только для BOARD_TYPE == 0)
// =====================================================================

#if BOARD_TYPE == 0
/**
 * @brief Включает один из портов датчиков (EG1-EG4) или выключает все.
 * @param port номер порта 1..4; при 0 – все выключаются.
 * @details Выбранный порт устанавливается в HIGH, остальные – в LOW.
 *          Состояние сохраняется в статической переменной isPortEnable.
 *          Если порт уже активен, выводится предупреждение "Double enable".
 *          После переключения выполняется задержка 500 мс для стабилизации.
 */
void enable_sens(int port) {
    if (port == isPortEnable) {
        Serial.printf("Duble enable %i\n", port);
    }
    switch (port) {
    case 1:
        digitalWrite(EG1, HIGH);
        Serial.println("port 1 ON");
        isPortEnable = 1;
        break;
    case 2:
        digitalWrite(EG2, HIGH);
        Serial.println("port 2 ON");
        isPortEnable = 2;
        break;
    case 3:
        digitalWrite(EG3, HIGH);
        Serial.println("port 3 ON");
        isPortEnable = 3;
        break;
    case 4:
        digitalWrite(EG4, HIGH);
        Serial.println("port 4 ON");
        isPortEnable = 4;
        break;
    default:
        digitalWrite(EG1, LOW);
        digitalWrite(EG2, LOW);
        digitalWrite(EG3, LOW);
        digitalWrite(EG4, LOW);
        isPortEnable = 0;
        Serial.println("port 1234 OFF");
        break;
    }
    delay(500);
}
#endif

// =====================================================================
// Управление LoRa и SIM (зависит от NET и BOARD_REV)
// =====================================================================

#if NET == 0 && BOARD_REV == 3
/**
 * @brief Включает/выключает питание LoRa-модуля для NET==0, rev.3.
 * @param act true = включить (ELORA HIGH), false = выключить (LOW).
 */
void enable_lora(bool act) {
    if (act == isLoraEnable)
        return;
    if (act) {
        digitalWrite(ELORA, HIGH);
        delay(200);
        Serial.println("LORA ON");
        isLoraEnable = true;
    } else {
        digitalWrite(ELORA, LOW);
        delay(200);
        Serial.println("LORA OFF");
        isLoraEnable = false;
    }
}

#elif NET == 1
/**
 * @brief Включает/выключает питание SIM-модуля для NET==1.
 * @param act true = включить (ESIM HIGH), false = выключить (LOW).
 */
void enable_sim(bool act) {
    if (act == isSimEnable)
        return;
    if (act) {
        digitalWrite(ESIM, HIGH);
        delay(200);
        Serial.println("SIM ON");
        isSimEnable = true;
    } else {
        digitalWrite(ESIM, LOW);
        delay(200);
        Serial.println("SIM OFF");
        isSimEnable = false;
    }
}

#elif NET == 2 && BOARD_REV == 3
/**
 * @brief Включает/выключает LoRa для NET==2, rev.3.
 * @param act true = включить (ELORA HIGH).
 */
void enable_lora(bool act) {
    if (act == isLoraEnable)
        return;
    if (act) {
        digitalWrite(ELORA, HIGH);
        delay(200);
        Serial.println("LORA ON");
        isLoraEnable = true;
    } else {
        digitalWrite(ELORA, LOW);
        delay(200);
        Serial.println("LORA OFF");
        isLoraEnable = false;
    }
}

/**
 * @brief Включает/выключает SIM для NET==2, rev.3.
 * @param act true = включить (ESIM HIGH).
 */
void enable_sim(bool act) {
    if (act == isSimEnable)
        return;
    if (act) {
        digitalWrite(ESIM, HIGH);
        delay(1000);
        Serial.println("SIM ON");
        isSimEnable = true;
    } else {
        digitalWrite(ESIM, LOW);
        delay(1000);
        Serial.println("SIM OFF");
        isSimEnable = false;
    }
}

/**
 * @brief Специальное управление SIM через общий сигнал EP (для NET==2).
 * @param act true – включить питание SIM (EP HIGH), false – выключить (EP LOW).
 * @note В отличие от enable_sim(), здесь управляется EP, а не ESIM.
 *       Используется, когда SIM и другое питание разделены.
 */
void activate_sim(bool act) {
    if (act) {
        digitalWrite(EP, LOW);
        Serial.println("SIM ON");
        delay(1000);
        digitalWrite(EP, HIGH);
    } else {
        digitalWrite(EP, LOW);
        Serial.println("SIM OFF");
        delay(3000);
        digitalWrite(EP, HIGH);
    }
}

#elif NET == 0 && BOARD_REV == 1
/**
 * @brief Управление LoRa для старой платы rev.1 (без реального пина).
 * @param act не влияет на аппаратуру, только меняет флаг и выводит сообщение.
 */
void enable_lora(bool act) {
    if (act) {
        Serial.println("LORA ON");
        isLoraEnable = true;
    } else {
        Serial.println("LORA OFF");
        isLoraEnable = false;
    }
}
#endif

// =====================================================================
// Функции CRC-16 (Modbus)
// =====================================================================

/**
 * @brief Вычисляет CRC-16 (Modbus) для блока данных и добавляет его в конец.
 * @param req исходные данные (без CRC)
 * @param dataLength длина данных
 * @param response выходной буфер размером dataLength+2, куда копируются данные
 *                 и дописываются два байта CRC (младший байт первым).
 * @details Алгоритм CRC-16/Modbus с полиномом 0xA001, начальным значением
 * 0xFFFF. Используется в протоколах RS485/Modbus.
 */
void addCRC(byte req[], int dataLength, byte response[]) {
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < dataLength; pos++) {
        crc ^= (uint16_t)req[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    // Копируем данные
    for (int i = 0; i < dataLength; i++)
        response[i] = req[i];
    // Добавляем CRC (LSB first)
    response[dataLength] = crc & 0xFF;
    response[dataLength + 1] = (crc >> 8) & 0xFF;
}

/**
 * @brief Вычисляет CRC-16 (Modbus) и возвращает только два байта CRC.
 * @param req данные
 * @param dataLength длина
 * @param outcrc массив из 2 байт (LSB, MSB) для результата.
 */
void outCRC(byte req[], int dataLength, byte outcrc[]) {
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < dataLength; pos++) {
        crc ^= (uint16_t)req[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    outcrc[0] = crc & 0xFF;
    outcrc[1] = (crc >> 8) & 0xFF;
}

// =====================================================================
// CRC-8 для датчика WH65LP
// =====================================================================

/**
 * @brief Вычисляет CRC-8 с полиномом 0x31 (алгоритм, аналогичный Dallas/MAXIM).
 * @param data указатель на данные
 * @param len длина в байтах
 * @return uint8_t вычисленное CRC.
 * @note Используется для проверки целостности данных от метеодатчика WH65LP.
 */
uint8_t crc8_wh65lp(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    uint8_t poly = 0x31;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ poly;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief Проверяет CRC-8 в ответе датчика WH65LP.
 * @param response буфер с ответом (должен содержать минимум 17 байт)
 * @param len общая длина буфера
 * @return true если CRC (байт 15) совпадает с вычисленным по первым 15 байтам.
 */
bool check_wh65lp_crc(uint8_t *response, uint8_t len) {
    if (len < 17)
        return false;
    uint8_t computed = crc8_wh65lp(response, 15);
    return (computed == response[15]);
}

// =====================================================================
// Проверка CRC-16 в произвольном ответе
// =====================================================================

/**
 * @brief Проверяет CRC-16 (Modbus) в принятом ответе.
 * @param response буфер с данными и двумя байтами CRC в конце
 * @param lenresponse общая длина буфера (включая CRC)
 * @return true если вычисленный CRC совпадает с переданным.
 * @note Выводит в Serial вычисленный CRC и результат проверки.
 */
bool checkCRC(byte response[], int lenresponse) {
    byte crc[2] = {0x00, 0x00};
    outCRC(response, lenresponse - 2, crc);
    printHEX(crc, 2);
    if (crc[1] == response[lenresponse - 1] &&
        crc[0] == response[lenresponse - 2]) {
        Serial.println("CRC OK");
        return true;
    } else {
        Serial.println("CRC BAD!");
        return false;
    }
}

// =====================================================================
// Функции для работы с системным временем
// =====================================================================

/**
 * @brief Выводит текущее системное время в формате YYYY-MM-DD HH:MM:SS.
 * @details Если время не установлено (меньше 2000 года), выводится сообщение.
 *          Используется localtime_r для потокобезопасности.
 */
void printCurrentTime() {
    time_t now;
    time(&now);
    if (now < 946684800) { // 1 Jan 2000
        Serial.println("no time, <2000 ");
        return;
    }
    struct tm timeinfo;
    struct tm *ti = localtime_r(&now, &timeinfo);
    if (!ti) {
        Serial.println("no time, null");
        return;
    }
    if (ti->tm_year < 120) { // год < 2020
        Serial.println("no time, <2020");
        return;
    }
    Serial.printf("my time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday, ti->tm_hour,
                  ti->tm_min, ti->tm_sec);
}

/**
 * @brief Проверяет, установлено ли корректное системное время.
 * @return true если время > 2000 года и (для BOARD_TYPE==1) не попадает в
 *         специальное окно 16:00-16:30 (используется для принудительного
 * обновления).
 * @note Для BOARD_TYPE==1 время в этом окне считается невалидным, чтобы
 *       вызывающий код мог запросить новое время с сервера.
 */
bool isTime() {
    time_t now;
    time(&now);
    if (now < 946684800) {
        Serial.println("no time, <2000 ");
        return false;
    }
    struct tm timeinfo;
    struct tm *ti = localtime_r(&now, &timeinfo);
    if (!ti) {
        Serial.println("no time, null");
        return false;
    }
    if (ti->tm_year < 120) {
        Serial.println("no time, <2020");
        return false;
    }
#if BOARD_TYPE == 1
    // Для плат типа 1 блокируем время с 16:00 до 16:30 – в этот период
    // приложение должно обновить время из внешнего источника.
    if (ti->tm_hour == 16 && ti->tm_min < 30) {
        Serial.printf("⏱ Time blocked: %02d:%02d (16:00-16:30 window)\n",
                      ti->tm_hour, ti->tm_min);
        return false;
    }
#endif
    return true;
}

/**
 * @brief Печатает время, упакованное в 6 байт, в читаемом формате.
 * @param buf массив [ГГ, ММ, ДД, ЧЧ, ММ, СС] (двоичные значения, не BCD).
 */
void printTimeFromHexBytes(const byte buf[6]) {
    if (!buf) {
        Serial.println("⚠️ printTimeFromHexBytes: null buffer");
        return;
    }
    uint8_t yy = buf[0], mm = buf[1], dd = buf[2];
    uint8_t hh = buf[3], mi = buf[4], ss = buf[5];
    Serial.printf("⏱ Time: [%02X %02X %02X %02X %02X %02X] → ", yy, mm, dd, hh,
                  mi, ss);
    Serial.printf("20%02d-%02d-%02d %02d:%02d:%02d\n", yy, mm, dd, hh, mi, ss);
}

/**
 * @brief Устанавливает системное время из 6 байт.
 * @param buf массив [ГГ, ММ, ДД, ЧЧ, ММ, СС] (двоичные значения, корректный
 * диапазон).
 * @return true если время успешно установлено, false при ошибке валидации или
 * вызове settimeofday.
 * @details Проверяет корректность месяца, дня, часа, минут и секунд.
 *          Преобразует в struct tm, затем в time_t и вызывает settimeofday.
 */
bool setTimeFromHexBytes(const byte buf[6]) {
    if (!buf)
        return false;
    uint8_t yy = buf[0], mm = buf[1], dd = buf[2];
    uint8_t hh = buf[3], mi = buf[4], ss = buf[5];

    // Валидация
    if (mm < 1 || mm > 12) {
        Serial.printf("⚠️ Invalid month: %d\n", mm);
        return false;
    }
    if (dd < 1 || dd > 31) {
        Serial.printf("⚠️ Invalid day: %d\n", dd);
        return false;
    }
    if (hh > 23 || mi > 59 || ss > 59) {
        Serial.println("⚠️ Invalid time components");
        return false;
    }

    struct tm timeinfo = {0};
    timeinfo.tm_year = (yy < 100) ? (yy + 100) : yy; // годы с 1900
    timeinfo.tm_mon = mm - 1;
    timeinfo.tm_mday = dd;
    timeinfo.tm_hour = hh;
    timeinfo.tm_min = mi;
    timeinfo.tm_sec = ss;
    timeinfo.tm_isdst = -1;

    time_t ts = mktime(&timeinfo);
    if (ts < 0) {
        Serial.println("⚠️ mktime() failed");
        return false;
    }

    struct timeval tv = {.tv_sec = ts, .tv_usec = 0};
    if (settimeofday(&tv, nullptr) != 0) {
        Serial.println("⚠️ settimeofday() failed");
        return false;
    }

    Serial.printf("✅ Time set: 20%02d-%02d-%02d %02d:%02d:%02d UTC\n", yy, mm,
                  dd, hh, mi, ss);
    return true;
}

/**
 * @brief Форматирует текущее время в строку "ГГ.ММ.ДД ЧЧ:ММ:СС".
 * @param buffer выходной буфер
 * @param buf_size размер буфера
 * @return int количество записанных символов (как у snprintf).
 */
int get_time_string(char *buffer, size_t buf_size) {
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    int year = (ti.tm_year + 1900) % 100;
    return snprintf(buffer, buf_size, "%02d.%02d.%02d %02d:%02d:%02d", year,
                    ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min,
                    ti.tm_sec);
}

/**
 * @brief Упаковывает текущее время в 64-битное целое.
 * @return uint64_t, где байты (старшие к младшим): 00, 00, ГГ, ММ, ДД, ЧЧ, ММ,
 * СС.
 * @details Каждое поле занимает ровно 8 бит, поэтому всё время помещается в 6
 * байт. Старшие два байта всегда нулевые.
 */
uint64_t getPackedTimeHex() {
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    int yy = (ti.tm_year + 1900) % 100;
    return ((uint64_t)yy << 40) | ((uint64_t)(ti.tm_mon + 1) << 32) |
           ((uint64_t)ti.tm_mday << 24) | ((uint64_t)ti.tm_hour << 16) |
           ((uint64_t)ti.tm_min << 8) | (uint64_t)ti.tm_sec;
}

/**
 * @brief Заполняет массив из 6 байт упакованным текущим временем.
 * @param buf массив [ГГ, ММ, ДД, ЧЧ, ММ, СС].
 * @see getPackedTimeHex()
 */
void getPackedTimeBytes(byte buf[6]) {
    uint64_t val = getPackedTimeHex();
    buf[0] = (val >> 40) & 0xFF;
    buf[1] = (val >> 32) & 0xFF;
    buf[2] = (val >> 24) & 0xFF;
    buf[3] = (val >> 16) & 0xFF;
    buf[4] = (val >> 8) & 0xFF;
    buf[5] = val & 0xFF;
}

// =====================================================================
// Преобразование RSSI в проценты (современная версия)
// =====================================================================

/**
 * @brief Преобразует байт RSSI от Ebyte в проценты качества сигнала.
 * @param rssiByte байт RSSI (0-255)
 * @return uint8_t значение от 0 до 100%.
 * @details Формула из мануала: dBm = -RSSI / 2.
 *          Диапазон: ≥ -30 дБм → 100%, ≤ -90 дБм → 0%.
 *          Промежуточные значения линейно интерполируются без использования
 * float.
 */
uint8_t rssiToPercent(uint8_t rssiByte) {
    int16_t dbm = -(rssiByte >> 1); // быстрое деление на 2

    if (dbm >= -30)
        return 100;
    if (dbm <= -90)
        return 0;

    // Линейная интерполяция: ((dbm + 90) * 100) / 60
    return (uint8_t)(((dbm + 90) * 100) / 60);
}

// =====================================================================
// Подготовка пакета данных
// =====================================================================

/**
 * @brief Заполняет буфер заголовком пакета: ID, батарея, время.
 * @param buf выходной буфер (полностью обнуляется)
 * @param len размер буфера
 * @param id идентификатор устройства (3 байта)
 * @param battery заряд батареи (0-100)
 * @param date массив из 6 байт [ГГ, ММ, ДД, ЧЧ, ММ, СС]
 * @return size_t длина полезных данных (всегда 11).
 * @note Формат: байт 0 не используется (оставлен для длины), байты 1-3 – ID,
 *       байт 4 – батарея, байты 5-10 – дата. Остальные байты буфера обнуляются.
 */
size_t preparePacket(uint8_t *buf, int len, uint32_t id, uint8_t battery,
                     byte date[]) {
    if (!buf)
        return 0;

    memset(buf, 0x00, len);

    buf[1] = (byte)(id >> 16) & 0xFF;
    buf[2] = (byte)(id >> 8) & 0xFF;
    buf[3] = (byte)(id) & 0xFF;
    buf[4] = battery;
    buf[5] = date[0];
    buf[6] = date[1];
    buf[7] = date[2];
    buf[8] = date[3];
    buf[9] = date[4];
    buf[10] = date[5];

    // Отладочный вывод
    Serial.printf("ID   %i  - %#02x %#02x %#02x\n", ID, buf[1], buf[2], buf[3]);
    Serial.printf("bat  %i          - %#02x\n", battery, buf[4]);
    Serial.print("data  ");
    uint16_t year = date[0] < 100 ? 2000 + date[0] : date[0];
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", year, date[1], date[2],
                  date[3], date[4], date[5]);
    printHEX(date, 6);

    return 11; // фиксированная длина
}

// =====================================================================
// Кодирование сообщения в буфер для передачи
// =====================================================================

/**
 * @brief Кодирует текстовое сообщение в буфер фиксированного формата (198
 * байт).
 * @param message входная строка (текст)
 * @param buffer выходной буфер (должен быть ≥ 198 байт)
 * @details Формат:
 *          байт 0: общая длина пакета
 *          байты 1-3: ID устройства (глобальная переменная ID)
 *          байты 4-6: 0xFF 0xFF 0xFF (маркер начала)
 *          байты 7-12: упакованное время (6 байт)
 *          байты 13 и далее: текст сообщения (до 185 символов)
 *          Оставшиеся байты заполняются нулями.
 */
void encode_to_buffer(const char *message, uint8_t *buffer) {
    const int buffer_size = 198;

    size_t msg_len = strlen(message);
    const size_t max_msg_len = buffer_size - 13; // 185
    if (msg_len > max_msg_len)
        msg_len = max_msg_len;

    int total_len = 1 + 3 + 3 + 6 + msg_len;
    if (total_len > buffer_size)
        total_len = buffer_size;

    buffer[0] = (uint8_t)total_len;

    buffer[1] = (uint8_t)((ID >> 16) & 0xFF);
    buffer[2] = (uint8_t)((ID >> 8) & 0xFF);
    buffer[3] = (uint8_t)(ID & 0xFF);

    buffer[4] = 0xFF;
    buffer[5] = 0xFF;
    buffer[6] = 0xFF;

    uint8_t time_bytes[6];
    getPackedTimeBytes(time_bytes);
    for (int i = 0; i < 6; i++) {
        buffer[7 + i] = time_bytes[i];
    }

    for (size_t i = 0; i < msg_len; i++) {
        buffer[13 + i] = (uint8_t)message[i];
    }

    // Обнуление хвоста
    int data_end = 13 + msg_len;
    for (int i = data_end; i < buffer_size; i++) {
        buffer[i] = 0;
    }
}