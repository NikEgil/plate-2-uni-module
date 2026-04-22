#include <sys.h>
esp_adc_cal_characteristics_t adc_chars;
Preferences preferences;

namespace {
bool isPowered = false; // Флаг состояния (сохраняется между вызовами)
int isPortEnable = 0;
bool isSimEnable = false;

} // namespace

void initPins() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(EG1, OUTPUT);
    pinMode(EG2, OUTPUT);
    pinMode(EG3, OUTPUT);
    pinMode(EG4, OUTPUT);
    pinMode(EP, OUTPUT);
    pinMode(ESIM, OUTPUT);

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

#define SWITCH_DEBOUNCE_MS 20

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
    // Проверяем причину пробуждения
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // Если разбудил EXT1 (внешний сигнал)
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t status = esp_sleep_get_ext1_wakeup_status();

        if (status & (1ULL << BUT1))
            return 1;
        if (status & (1ULL << BUT2))
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
    int vbat = v * 10;
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
        Serial.print("0x");
        if (data[j] < 0x10)
            Serial.print("0"); // Добавляем ведущий ноль для однозначных HEX
        Serial.print(data[j], HEX);
        if (j != len - 1)
            Serial.print(", ");
    }
    Serial.println();
}

void saveArrayToFlash(byte data[]) {
    byte t[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
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

void enable_power(bool act) {
    // Если состояние уже соответствует запросу — ничего не делаем
    if (act == isPowered) {
        return;
    }
    if (act) {
        // pinMode(E12V, OUTPUT);
        digitalWrite(EP, HIGH);
        delay(200);
        Serial.println("POWER ON");
        isPowered = true;
    } else {
        digitalWrite(EP, LOW);
        delay(200);
        Serial.println("POWER OFF");
        isPowered = false;
    }
}
void enable_sens(int port) {
    if (port == isPortEnable) {
        Serial.printf("Duble enable %i\n", port);
        return;
    }
    switch (port) {
    case 1:
        // pinMode(EG1, OUTPUT);
        digitalWrite(EG1, HIGH);
        Serial.println("port 1 ON");
        break;
    case 2:
        // pinMode(EG2, OUTPUT);
        digitalWrite(EG2, HIGH);
        Serial.println("port 2 ON");
        break;
    case 3:
        // pinMode(EG3, OUTPUT);
        digitalWrite(EG3, HIGH);
        Serial.println("port 3 ON");
        break;
    case 4:
        // pinMode(EG4, OUTPUT);
        digitalWrite(EG4, HIGH);
        Serial.println("port 4 ON");
        break;
    default:
        digitalWrite(EG1, LOW);
        digitalWrite(EG2, LOW);
        digitalWrite(EG3, LOW);
        digitalWrite(EG4, LOW);
        Serial.println("port 1234 OFF");
        break;
    }
    delay(500);
}
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

void printCurrentTime() {
    time_t now;
    time(&now);

    // Проверка: время не установлено (эпоха 1970)
    if (now < 946684800) { // 1 Jan 2000 00:00:00 UTC
        Serial.println("false");
        return;
    }

    struct tm timeinfo;
    // localtime_r может вернуть NULL при ошибке
    struct tm *ti = localtime_r(&now, &timeinfo);
    if (!ti) {
        Serial.println("false");
        return;
    }

    // Проверка на разумные значения
    if (ti->tm_year < 120) { // Год < 2020
        Serial.println("false");
        return;
    }

    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", ti->tm_year + 1900,
                  ti->tm_mon + 1, ti->tm_mday, ti->tm_hour, ti->tm_min,
                  ti->tm_sec);
}


void printPackedTime(const uint8_t* dateBytes) {
    if (!dateBytes) return;
    
    uint16_t year = dateBytes[0] < 100 ? 2000 + dateBytes[0] : dateBytes[0];
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
        year, dateBytes[1], dateBytes[2], dateBytes[3], dateBytes[4], dateBytes[5]);
}


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

void getPackedTimeBytes(byte buf[6]) {
    uint64_t val = getPackedTimeHex();
    buf[0] = (val >> 40) & 0xFF;
    buf[1] = (val >> 32) & 0xFF;
    buf[2] = (val >> 24) & 0xFF;
    buf[3] = (val >> 16) & 0xFF;
    buf[4] = (val >> 8) & 0xFF;
    buf[5] = val & 0xFF;
}

size_t preparePacket(uint8_t *buf, uint32_t id, uint8_t battery, byte date[],
                     uint8_t signal1, uint8_t signal2) {
    if (!buf)
        return 0;

    // 1. Полная очистка буфера (0-199 = 0x00)
    memset(buf, 0x00, 200);

    // 2. ID: 3 байта, старший байт первый (Big-Endian)
    // Пример: ID=0x123456 → [0x12][0x34][0x56]

    buf[0] = (byte)(id >> 16) & 0xFF;
    buf[1] = (byte)(id >> 8) & 0xFF;
    buf[2] = (byte)(id) & 0xFF;
    // 3. Заряд батареи: 1 байт (0-100%)
    buf[3] = battery;

    // 4. Дата: 6 байт (YY, MM, DD, HH, MM, SS)

    buf[4] = date[0];
    buf[5] = date[1];
    buf[6] = date[2];
    buf[7] = date[3];
    buf[8] = date[4];
    buf[9] = date[5];

    // 5. Качество сигнала: 2 байта
    buf[10] = signal1;
    buf[11] = signal2;
    Serial.printf("ID   %i - %#02x %#02x %#02x\n", ID, buf[0], buf[1], buf[2]);
    Serial.printf("bat  %i          - %#02x\n", battery, buf[3]);
    Serial.print("data  ");
    printPackedTime(date);
    printHEX(date, 6);
    Serial.printf("signal1  %i - %#02x\n", signal1, buf[10]);
    Serial.printf("signal2  %i - %#02x\n", signal2, buf[11]);
    // Bytes 12-199 уже заполнены нулями через memset

    return 12; // Возвращаем длину полезных данных
}
