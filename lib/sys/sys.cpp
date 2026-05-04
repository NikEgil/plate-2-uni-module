#include <sys.h>
esp_adc_cal_characteristics_t adc_chars;
Preferences preferences;

namespace {
bool isPowered = false; // Флаг состояния (сохраняется между вызовами)
int isPortEnable = 0;
bool isSimEnable = false;
bool isLoraEnable = false;
} // namespace
#if BOARD_REV == 2 
void initPins() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(EG1, OUTPUT);
    pinMode(EG2, OUTPUT);
    pinMode(EG3, OUTPUT);
    pinMode(EG4, OUTPUT);
    pinMode(E12V, OUTPUT);
        pinMode(E5V, OUTPUT);

    pinMode(ADC, INPUT);
    analogReadResolution(13);
    analogSetAttenuation(ADC_6db);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_13, 3300,
                             &adc_chars);
    pinMode(BUT1, INPUT_PULLUP); // Кнопка NO: в покое HIGH, при нажатии LOW
    pinMode(BUT2, INPUT_PULLUP); // Кнопка NO: в покое HIGH, при нажатии LOW
    uint64_t btnMask = (1ULL << BUT1) | (1ULL << BUT2);

    // Настраиваем пробуждение по любому из этих пинов (LOW)
}
#elif BOARD_REV == 3 and BOARD_TYPE == 0
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
    pinMode(BUT1, INPUT_PULLUP); // Кнопка NO: в покое HIGH, при нажатии LOW
    pinMode(BUT2, INPUT_PULLUP); // Кнопка NO: в покое HIGH, при нажатии LOW
    uint64_t btnMask = (1ULL << BUT1) | (1ULL << BUT2);

    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
    // Настраиваем пробуждение по любому из этих пинов (LOW)
    esp_sleep_enable_ext1_wakeup(btnMask, ESP_EXT1_WAKEUP_ANY_LOW);
}
#elif BOARD_REV == 3 and BOARD_TYPE == 1
void initPins() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(ESIM, OUTPUT);
    pinMode(EP, OUTPUT);
    pinMode(ELORA, OUTPUT);
    // настройка для измерения батареи
    pinMode(ADC, INPUT);
    analogReadResolution(13);
    analogSetAttenuation(ADC_6db);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_13, 3300,
                             &adc_chars);
    pinMode(BUT1, INPUT_PULLUP); // Кнопка NO: в покое HIGH, при нажатии LOW
    pinMode(BUT2, INPUT_PULLUP); // Кнопка NO: в покое HIGH, при нажатии LOW
    uint64_t btnMask = (1ULL << BUT1) | (1ULL << BUT2);

    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
    // Настраиваем пробуждение по любому из этих пинов (LOW)
    esp_sleep_enable_ext1_wakeup(btnMask, ESP_EXT1_WAKEUP_ANY_LOW);
}

#endif
byte rssip(byte rssi) {
    float rssi1 = (float)rssi;
    int16_t dbm = (int)-(rssi1 / 2);
    int perc = (uint8_t)(((dbm + 128) * 100) / 108);
    Serial.printf("rssi get %f,perc %i\n", rssi1, perc);
    return (byte)perc;
}
uint8_t readSwitchState() {
    static uint8_t lastRaw = 0;
    static uint8_t lastStable = 0;
    static unsigned long lastChange = 0;
    // Читаем физическое состояние (INPUT_PULLUP: LOW = включено)
    uint8_t raw = 0;
    if (digitalRead(SW1_PIN) == LOW)
        raw |= 0x01; // Бит 0
    if (digitalRead(SW2_PIN) == LOW)
        raw |= 0x02; // Бит 1

    // Сброс таймера при изменении
    if (raw != lastRaw) {
        lastRaw = raw;
        lastChange = millis();
    }

    // Если состояние стабильно > дебаунс — принимаем его
    if (millis() - lastChange > SWITCH_DEBOUNCE_MS) {
        if (raw != lastStable) {
            lastStable = raw;
            return raw; // Возвращаем только при реальном изменении
        }
    }

    return 0xFF; // Специальный код: "нет изменений" (можно игнорировать)
}

uint8_t checkButton() {

    uint64_t status = 0;

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        status = esp_sleep_get_ext1_wakeup_status();
    }

    delay(10); // Дебаунс

    // Проверка по статусу пробуждения ИЛИ прямое чтение пинов
    bool b1 = (status & (1ULL << BUT1)) || (digitalRead(BUT1) == LOW);
    bool b2 = (status & (1ULL << BUT2)) || (digitalRead(BUT2) == LOW);

    if (b1) {
        waitForButtonRelease();
        return 1;
    }
    if (b2) {
        waitForButtonRelease();
        return 2;
    }

    return 0;
}

void waitForButtonRelease() {
    // Ждем HIGH на обеих кнопках (отпускания)
    // Это защита от дребезга и удержания кнопки при уходе в сон
    Serial.println("Waiting for release...");
    for (int i = 0; i < 20; i++) {
        if (digitalRead(BUT1) == LOW || digitalRead(BUT2) == LOW) {
            return;
        }
        delay(10);
    }
}

void sleep(int time) {
    esp_sleep_enable_timer_wakeup(time * uS_TO_S_FACTOR);
    Serial.printf("                 Sleep %i sec", time);
    Serial.flush();
    delay(500);
    esp_deep_sleep_start();
}

int readBatteryVoltage() {
    uint32_t raw = 0;
    for (int i = 0; i < 10; i++) {
        raw += analogReadRaw(ADC);
        delay(20);
    }
    raw /= 10;

    // Линейная интерполяция: Vbat = k * raw + b
    float k = (CAL_HIGH.vbat - CAL_LOW.vbat) / (CAL_HIGH.raw - CAL_LOW.raw);
    float b = CAL_LOW.vbat - k * CAL_LOW.raw;

    float v = k * raw + b;
    int vbat = (int)(v * 10);
    Serial.printf("RAW:%i, BAT: %.3f V, int:%i \n", raw, v, vbat);
    return vbat;
}

void blink(int count, int delayy) {
    for (int i = 0; i < count; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayy);
        digitalWrite(LED_PIN, LOW);
        delay(delayy);
    }
}

void printHEX(byte data[], int len) {
    if (len == 0) {
        Serial.println("NONE HEX");
    }
    for (int j = 0; j < len; j++) {
        // Serial.print("0x");
        if (data[j] < 0x10)
            Serial.print("0"); // Добавляем ведущий ноль для однозначных HEX
        Serial.print(data[j], HEX);
        if (j != len - 1)
            Serial.print(" ");
    }
    Serial.println();
}

void saveArrayToFlash(byte data[]) {
    preferences.begin("my-data",
                      false); // Открываем пространство имен "my-data"
    preferences.putBytes("array", data, 8);

    preferences.end();
    Serial.print("tableSens:		");
    printHEX(data, 5);
}

bool loadArrayFromFlash(byte data[]) {
    preferences.begin("my-data", true); // Открыть в режиме чтения
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

void byteArrayToHexString(const byte *byteArray, int length, String str) {
    for (int i = 0; i < length; i++) {
        if (i > 0) {
            str += " ";
        }
        str += String(byteArray[i], HEX); // вывод в HEX
    }
}

#if BOARD_REV == 2
void enable_power(bool act) {
    if (act == isPowered) {
        return;
    }
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
void enable_power(bool act) {
    // Если состояние уже соответствует запросу — ничего не делаем
    if (act == isPowered) {
        return;
    }
    if (act) {
        digitalWrite(EP, HIGH);
        delay(400);
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

#if BOARD_TYPE == 0
void enable_sens(int port) {
    if (port == isPortEnable) {
        Serial.printf("Duble enable %i\n", port);
    }
    switch (port) {
    case 1:
        // pinMode(EG1, OUTPUT);
        digitalWrite(EG1, HIGH);
        Serial.println("port 1 ON");
        isPortEnable = 1;
        break;
    case 2:
        // pinMode(EG2, OUTPUT);
        digitalWrite(EG2, HIGH);
        Serial.println("port 2 ON");
        isPortEnable = 2;
        break;
    case 3:
        // pinMode(EG3, OUTPUT);
        digitalWrite(EG3, HIGH);
        Serial.println("port 3 ON");
        isPortEnable = 3;
        break;
    case 4:
        // pinMode(EG4, OUTPUT);
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

#if NET == 0 and BOARD_REV == 3
void enable_lora(bool act) {
    if (act == isLoraEnable) {
        return;
    }
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
void enable_sim(bool act) {
    if (act == isSimEnable) {
        return;
    }
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
#elif NET == 2
void enable_lora(bool act) {
    if (act == isLoraEnable) {
        return;
    }
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
void enable_sim(bool act) {
    if (act == isSimEnable) {
        return;
    }
    if (act) {
        digitalWrite(ESIM, HIGH);
        delay(2000);
        Serial.println("SIM ON");
        isSimEnable = true;
    } else {
        digitalWrite(ESIM, LOW);
        delay(200);
        Serial.println("SIM OFF");
        isSimEnable = false;
    }
}
#endif

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

    // Копируем исходные данные в результат
    for (int i = 0; i < dataLength; i++) {
        response[i] = req[i];
    }

    // Добавляем CRC в конец (младший байт первый)
    response[dataLength] = crc & 0xFF;            // LSB
    response[dataLength + 1] = (crc >> 8) & 0xFF; // MSB
}

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
    // Добавляем CRC в конец (младший байт первый)
    outcrc[0] = crc & 0xFF;        // LSB
    outcrc[1] = (crc >> 8) & 0xFF; // MSB
}
bool checkCRC(byte response[], int lenresponse) {
    byte crc[2] = {0x00, 0x00};
    outCRC(response, lenresponse - 2, crc);
    printHEX(crc, 2);
    // Serial.println(response[lenresponse-2],HEX);
    // Serial.println(response[lenresponse-1],HEX);
    if (crc[1] == response[lenresponse - 1] &
        crc[0] == response[lenresponse - 2]) {
        Serial.println("CRC OK");
        return true;
    } else {
        Serial.println("CRC BAD!");
        return false;
    }
}
// вывод текущего времени, если нет то false
void printCurrentTime() {
    time_t now;
    time(&now);
    // Проверка: время не установлено (эпоха 1970)
    if (now < 946684800) { // 1 Jan 2000 00:00:00 UTC
        Serial.println("no time, <2000 ");
        return;
    }
    struct tm timeinfo;
    // localtime_r может вернуть NULL при ошибке
    struct tm *ti = localtime_r(&now, &timeinfo);
    if (!ti) {
        Serial.println("no time, null");
        return;
    }
    // Проверка на разумные значения
    if (ti->tm_year < 120) { // Год < 2020
        Serial.println("no time, <2020");
        return;
    }
    Serial.printf("my time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday, ti->tm_hour,
                  ti->tm_min, ti->tm_sec);
}

// возват true если время правдоподобное
bool isTime() {
    time_t now;
    time(&now);

    // Проверка на валидность времени (эпоха)
    if (now < 946684800) { // 1 Jan 2000 00:00:00 UTC
        Serial.println("no time, <2000 ");
        return false;
    }

    struct tm timeinfo;
    struct tm *ti = localtime_r(&now, &timeinfo);
    if (!ti) {
        Serial.println("no time, null");
        return false;
    }

    // Проверка на разумные значения года
    if (ti->tm_year < 120) { // Год < 2020
        Serial.println("no time, <2020");
        return false;
    }

    // 🔹 НОВАЯ ПРОВЕРКА: если час 0 И минуты < 30 → возврат false
    // (Интервал 00:00 ... 00:29:59)
    if (ti->tm_hour == 16 && ti->tm_min < 30) {
        Serial.printf("⏱ Time blocked: %02d:%02d (00:00-00:30 window)\n",
                      ti->tm_hour, ti->tm_min);
        return false;
    }

    return true;
}
void printTimeFromHexBytes(const byte buf[6]) {
    if (!buf) {
        Serial.println("⚠️ printTimeFromHexBytes: null buffer");
        return;
    }

    // 🔹 Извлекаем значения
    uint8_t yy = buf[0]; // 00-99
    uint8_t mm = buf[1]; // 1-12
    uint8_t dd = buf[2]; // 1-31
    uint8_t hh = buf[3]; // 0-23
    uint8_t mi = buf[4]; // 0-59
    uint8_t ss = buf[5]; // 0-59

    // 🔹 Вывод в формате: HEX и человекочитаемом
    Serial.printf("⏱ Time: [%02X %02X %02X %02X %02X %02X] → ", yy, mm, dd, hh,
                  mi, ss);
    Serial.printf("20%02d-%02d-%02d %02d:%02d:%02d\n", yy, mm, dd, hh, mi, ss);
}
bool setTimeFromHexBytes(const byte buf[6]) {
    if (!buf)
        return false;

    // 🔹 1. Извлекаем значения
    uint8_t yy = buf[0]; // 00-99
    uint8_t mm = buf[1]; // 1-12
    uint8_t dd = buf[2]; // 1-31
    uint8_t hh = buf[3]; // 0-23
    uint8_t mi = buf[4]; // 0-59
    uint8_t ss = buf[5]; // 0-59

    // 🔹 2. Валидация диапазонов
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

    // 🔹 3. Конвертируем в struct tm
    struct tm timeinfo = {0};
    timeinfo.tm_year =
        (yy < 100) ? (yy + 100) : yy; // years since 1900 (2000 = 100)
    timeinfo.tm_mon = mm - 1;         // 0-based month [0-11]
    timeinfo.tm_mday = dd;
    timeinfo.tm_hour = hh;
    timeinfo.tm_min = mi;
    timeinfo.tm_sec = ss;
    timeinfo.tm_isdst = -1; // auto-detect DST

    // 🔹 4. Конвертируем в time_t (Unix timestamp)
    time_t ts = mktime(&timeinfo);
    if (ts < 0) {
        Serial.println("⚠️ mktime() failed");
        return false;
    }

    // 🔹 5. Устанавливаем системное время
    struct timeval tv = {.tv_sec = ts, .tv_usec = 0};

    if (settimeofday(&tv, nullptr) != 0) {
        Serial.println("⚠️ settimeofday() failed");
        return false;
    }

    // 🔹 6. Логирование
    Serial.printf("✅ Time set: 20%02d-%02d-%02d %02d:%02d:%02d UTC\n", yy, mm,
                  dd, hh, mi, ss);

    return true;
}

// подготовительная функция
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

// получение текущей даты в hex ГГ ММ ДД ЧЧ ММ СС
void getPackedTimeBytes(byte buf[6]) {
    uint64_t val = getPackedTimeHex();
    buf[0] = (val >> 40) & 0xFF;
    buf[1] = (val >> 32) & 0xFF;
    buf[2] = (val >> 24) & 0xFF;
    buf[3] = (val >> 16) & 0xFF;
    buf[4] = (val >> 8) & 0xFF;
    buf[5] = val & 0xFF;
}
uint8_t rssiToPercent(uint8_t rssiByte) {
    // Формула из мануала Ebyte (стр. 16): dBm = -RSSI / 2
    int16_t dbm = -(rssiByte >> 1); // Быстрое деление на 2

    // Практический диапазон LoRa:
    // ≥ -30 dBm → 100% (очень близко / идеально)
    // ≤ -90 dBm →   0% (шумовой порог / обрыв)
    if (dbm >= -30)
        return 100;
    if (dbm <= -90)
        return 0;

    // Линейная интерполяция без float (экономит такты ESP32)
    return (uint8_t)(((dbm + 90) * 100) / 60);
}
// подготовка начала пакета для отправления
size_t preparePacket(uint8_t *buf, int len, uint32_t id, uint8_t battery,
                     byte date[]) {
    if (!buf)
        return 0;

    // 1. Полная очистка буфера (0-199 = 0x00)
    memset(buf, 0x00, len);
    // 2. ID: 3 байта, старший байт первый (Big-Endian)
    // Пример: ID=0x123456 → [0x12][0x34][0x56]
    buf[1] = (byte)(id >> 16) & 0xFF;
    buf[2] = (byte)(id >> 8) & 0xFF;
    buf[3] = (byte)(id) & 0xFF;
    // 3. Заряд батареи: 1 байт (0-100%)
    buf[4] = battery;
    // 4. Дата: 6 байт (YY, MM, DD, HH, MM, SS)
    buf[5] = date[0];
    buf[6] = date[1];
    buf[7] = date[2];
    buf[8] = date[3];
    buf[9] = date[4];
    buf[10] = date[5];
    // 5. Качество сигнала: 2 байта
    Serial.printf("ID   %i  - %#02x %#02x %#02x\n", ID, buf[1], buf[2], buf[3]);
    Serial.printf("bat  %i          - %#02x\n", battery, buf[4]);
    Serial.print("data  ");
    uint16_t year = date[0] < 100 ? 2000 + date[0] : date[0];
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", year, date[1], date[2],
                  date[3], date[4], date[5]);
    printHEX(date, 6);
    // Bytes 12-199 уже заполнены нулями через memset

    return 11; // Возвращаем длину полезных данных
}
