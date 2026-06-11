Now I have a complete picture of the firmware. Let me compile the full audit.

---

# 🔍 Полный аудит прошивки устройства

## Архитектура проекта

| Компонент | Назначение |
|-----------|-----------|
| `src/main.cpp` (1762 строки) | Главный модуль: setup/loop, polling сенсоров, формирование пакетов |
| `lib/def/` | Определения пинов, калибровка АЦП, константы |
| `lib/lora/` | Драйвер LoRa E220 |
| `lib/rs/` | RS485 Modbus (только для ПМ) |
| `lib/sim/` | SIM800 GSM/GPRS + HTTP |
| `lib/stack/` | FlashStack — FIFO-хранилище пакетов во flash |
| `lib/sys/` | Утилиты: пины, питание, CRC, сон, время |

Конфигурация:
- **Головной модуль (ГМ):** `BOARD_REV=3, NET=2, BOARD_TYPE=1` — LoRa + SIM, без RS485
- **Полевой модуль (ПМ):** `BOARD_REV=1, NET=0, BOARD_TYPE=0` — только LoRa, с RS485

---

## 🔴 КРИТИЧЕСКИЕ ПРОБЛЕМЫ (могут привести к крашу/потере данных)

### 1. Утечка памяти в SIM-модуле (`lib/sim/sim.cpp`)

```cpp
void begin(...) {
    end();
    modem = new TinyGsm(*simSerial);   // выделяем
    client = new TinyGsmClient(*modem); // выделяем
}

void end() {
    if (simSerial) { simSerial->end(); }
    isActive = false;
    // ❌ НЕТ delete modem; delete client;
}
```

**Проблема:** каждый вызов `begin()` выделяет ~200+ байт через `new`, но `end()` не освобождает. После нескольких циклов сон→просыпание — heap exhaustion → crash.

**Исправление:**
```cpp
void end() {
    if (client) { delete client; client = nullptr; }
    if (modem) { delete modem; modem = nullptr; }
    if (simSerial) { simSerial->end(); }
    isActive = false;
}
```

---

### 2. FlashStack: RTC_NOINIT_ATTR — мусор после сбоя питания (`lib/stack/FlashStack.cpp`)

```cpp
static volatile RTC_NOINIT_ATTR flash_state_t fstate;
```

`RTC_NOINIT_ATTR` сохраняет данные между deep sleep, но **инициализируется мусором** после полного сбоя питания. Если в `begin()` magic number случайно совпадёт — структура будет использоваться с мусорными указателями.

**Проблема:** нет CRC или checksum для `fstate`. Достаточно одного битого значения `head_sector` чтобы записать в произвольный сектор flash.

**Исправление:** добавить CRC в `flash_state_t` и проверять при загрузке:
```cpp
typedef struct {
    // ... существующие поля ...
    uint32_t crc32; // CRC от всех полей выше
} flash_state_t;
```

---

### 3. Конфликт Serial1 между RS485 и SIM (`lib/rs/rs.cpp` + `lib/sim/sim.cpp`)

```cpp
// rs.cpp — SoftwareSerial
SoftwareSerial rsSerial;
rsSerial.begin(9600, SWSERIAL_8N1, rx, tx);

// sim.cpp — HardwareSerial
simSerial = &Serial1;
simSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
```

**Проблема:** при `NET=2` (оба) и `BOARD_TYPE=0` оба модуля могут пытаться использовать один и тот же UART. Хотя текущая конфигурация ГМ (`BOARD_TYPE=1`) не включает RS485, код не защищён от ошибочной конфигурации.

**Исправление:** добавить `static_assert`:
```cpp
#if NET == 2 && BOARD_TYPE == 0
#error "RS485 and SIM cannot coexist on the same UART"
#endif
```

---

### 4. Переполнение буфера пакета (`src/main.cpp`)

```cpp
byte packet[200]; // локальный буфер
// ...
addCRC(packet, len, packet); // CRC пишет в packet[len] и packet[len+1]
```

`FlashStack::DATA_LENGTH = 198`, но буфер 200 байт. Если `len == 199`, CRC запишет в `packet[199]` и `packet[200]` — **выход за границы массива**.

**Исправление:**
```cpp
byte packet[FlashStack::DATA_LENGTH + 2]; // 200 байт, но привязано к константе
if (len > FlashStack::DATA_LENGTH - 2) {
    len = FlashStack::DATA_LENGTH - 2; // гарантировать место для CRC
}
```

---

### 5. Нет watchdog-feeding в длинных блокировках (`src/main.cpp`)

```cpp
// Ожидание метеостанции: до 80 × 250мс = 20 секунд
for (int i = 0; i < 80; i++) {
    delay(250); // ❌ нет esp_task_wdt_reset()
    // ...
}
```

**Проблема:** Task WDT по умолчанию ~5 секунд. 20-секундный цикл без сброса watchdog → принудительный reset.

**Исправление:**
```cpp
for (int i = 0; i < 80; i++) {
    delay(250);
    esp_task_wdt_reset(); // ← добавить
}
```

---

## 🟠 СЕРЬЁЗНЫЕ ПРОБЛЕМЫ (ненадёжность, потеря пакетов)

### 6. Жёстко зашитый ID устройства (`lib/def/defenitions.cpp`)

```cpp
const int ID = 10010; // ❌ захардкожено
```

Калибровка АЦП зависит от ID. При сборке нового устройства с другим ID — **неправильное напряжение батареи**, неверные решения о сне/пробуждении.

**Исправление:** хранить ID в `Preferences` или `eFuse`, читать при старте.

---

### 7. HTTP: закомментированная логика переподключения (`lib/sim/sim.cpp:811-822`)

```cpp
if (!modem->isGprsConnected()) {
    Serial.println("[HTTP] GPRS lost, reconnecting...");
    return false; // ❌ сразу возврат, вместо переподключения
    // if (!connect(apn, gprsUser, gprsPass)) { ... }
}
```

**Проблема:** при потере GPRS пакет теряется без попытки восстановления.

**Исправление:** раскомментировать и реализовать retry с backoff.

---

### 8. Нет retry при отправке LoRa (`src/main.cpp`)

```cpp
if (LoRa::send(packet, len + 2)) {
    stack.pop(); // удаляем из стека
} else {
    // просто логируем, пакет остаётся в стеке
}
```

**Проблема:** если LoRa-модуль временно недоступен (помехи, разряд), пакеты копятся в flash. При 6000 записях и частоте опроса 15 минут — flash заполнится за ~62 дня.

**Исправление:** добавить метку времени в пакет и TTL-удаление старых записей.

---

### 9. `String logBuf` — фрагментация heap (`src/main.cpp:34`)

```cpp
String logBuf;
const size_t MAX_LOG = 3000;
// ...
while (logBuf.length() + 1 > MAX_LOG)
    logBuf.remove(0, TRIM_SIZE); // ❌ O(n) копирование каждый байт
logBuf += (char)c;               // ❌ посимвольная конкатенация
```

**Проблема:** `String::remove(0, N)` сдвигает весь буфер. При 3000 байтах и интенсивном логировании — heap fragmentation → crash.

**Исправление:** использовать кольцевой буфер `char logBuf[MAX_LOG]` с указателями head/tail.

---

### 10. Калибровка АЦП: нет защиты от неверных значений (`lib/def/defenitions.cpp`)

```cpp
const CalPoint CAL_LOW = (ID == 10010) ? CalPoint{4335, 3.126}
                         : CalPoint{3697, 3}; // fallback
```

**Проблема:** если ID не найден в таблице — используются значения по умолчанию `{3697, 3.0}`, которые могут не соответствовать реальной плате. Нет флага "калибровка не найдена".

**Исправление:**
```cpp
bool calibrationValid = (ID == 21 || ID == 22 || /* ... */);
if (!calibrationValid) {
    Serial.println("⚠️ No calibration for this ID!");
}
```

---

### 11. Непоследовательное управление питанием (`lib/sys/sys.cpp`)

```cpp
void enable_power(bool act) {
    digitalWrite(EP, act ? HIGH : LOW);
    isPowered = act;
    delay(500); // ❌ 500мс блокировки каждый раз
}
```

**Проблема:** нет проверки текущего состояния перед переключением. Нет гарантии стабилизации питания перед работой с периферией.

**Исправление:**
```cpp
void enable_power(bool act) {
    if (isPowered == act) return; // уже в нужном состоянии
    digitalWrite(EP, act ? HIGH : LOW);
    isPowered = act;
    if (act) delay(500); // задержка только при включении
}
```

---

## 🟡 СРЕДНИЕ ПРОБЛЕМЫ (качество кода, потенциальные баги)

### 12. Использование `and` вместо `&&` в `#if` (дефект совместимости)

```cpp
#if BOARD_REV == 3 and BOARD_TYPE == 0  // ❌ нестандартно
#if BOARD_REV == 3 && BOARD_TYPE == 0   // ✅ стандарт
```

Работает в GCC/Clang, но не portable и может сломаться при смене компилятора.

---

### 13. Дублирование CRC-кода (`lib/sys/sys.cpp`)

```cpp
void addCRC(byte req[], int dataLength, byte response[]); // CRC16 Modbus
void outCRC(byte req[], int dataLength, byte outcrc[]);   // CRC16 Modbus — ДУБЛИКАТ
```

Обе функции делают одно и то же (CRC16 Modbus). `addCRC` копирует данные + CRC, `outCRC` только CRC. Логика одинаковая.

**Исправление:** оставить одну функцию, вторую — обёртку.

---

### 14. `polling()` — нет валидации ответа сенсора (`src/main.cpp:242`)

```cpp
int polling(int sens, int lenreg, uint8_t *outBuf, size_t outBufSize) {
    if (!outBuf || outBufSize < 5) return -1;
    // ... отправка запроса ...
    // ❌ нет проверки CRC ответа
    // ❌ нет проверки длины ответа
    // ❌ нет проверки Modbus exception codes
}
```

**Исправление:** добавить проверку CRC16 Modbus, длины и кодов исключений (0x80+function).

---

### 15. Отсутствие `volatile` для переменных, используемых в прерываниях

```cpp
namespace {
bool isPowered = false;     // ❌ не volatile
int isPortEnable = 0;       // ❌ не volatile
}
```

Если эти переменные читаются/пишутся из ISR (кнопки, таймеры) — компилятор может кэшировать значения в регистре.

---

### 16. `FlashStack::begin()` — нет версионирования формата

```cpp
static const size_t MAGIC = 0x4653544B; // "FSTK"
```

При изменении `DATA_LENGTH` или `record_t` — старые данные во flash станут нечитаемыми, но magic number совпадёт → **коррупция данных**.

**Исправление:** добавить версию формата в magic:
```cpp
static const size_t MAGIC = 0x4653544B + FORMAT_VERSION;
```

---

### 17. Нет graceful degradation при недоступности сенсоров

```cpp
for (int port = 0; port < 2; port++) {
    mRes = measure(activeport[port]);
    if (mRes.valid) { /* ... */ }
    else {
        Serial.println("⚠️ No valid measured data to append");
        // ❌ пакет всё равно отправляется с нулевыми данными
    }
}
```

**Проблема:** получатель не может отличить "данные = 0" от "сенсор не ответил".

**Исправление:** использовать специальное значение (0xFFFF или флаг в заголовке) для "нет данных".

---

### 18. `sleep()` — нет корректного завершения SIM (`src/main.cpp`)

```cpp
void sleep(int time) {
    // ... настройка таймера ...
    esp_deep_sleep_start();
    // ❌ нет вызова SimModule::end() перед сном
}
```

**Проблема:** SIM-модуль может остаться в незавершённом состоянии → повышенное потребление во сне.

---

### 19. Magic number 198/200 разбросан по коду

```cpp
// FlashStack.h
static const size_t DATA_LENGTH = 198;

// main.cpp
byte packet[200];
if (len < 198) { memset(packet + len, 0x00, 198 - len); }
```

**Исправление:** везде использовать `FlashStack::DATA_LENGTH` и `sizeof(packet)`.

---

### 20. Нет таймаута на RS485 receive (`lib/rs/rs.cpp`)

```cpp
size_t receiveData(byte *buffer, size_t maxLen, uint32_t silenceTimeout_ms) {
    while (millis() - start < silenceTimeout_ms + 500) {
        // ❌ общий таймаут = silence + 500мс, может блокировать до 510мс
    }
}
```

**Проблема:** при отсутствии ответа функция блокирует на 510мс. В цикле опроса двух портов — до 1 секунды блокировки без watchdog-feeding.

---

## 📋 Сводная таблица

| # | Серьёзность | Файл | Проблема |
|---|-------------|------|----------|
| 1 | 🔴 Critical | `sim.cpp` | Утечка памяти (new без delete) |
| 2 | 🔴 Critical | `FlashStack.cpp` | RTC_NOINIT без checksum |
| 3 | 🔴 Critical | `rs.cpp` + `sim.cpp` | Конфликт Serial1 |
| 4 | 🔴 Critical | `main.cpp` | Потенциальный buffer overflow |
| 5 | 🔴 Critical | `main.cpp` | Watchdog timeout в длинных циклах |
| 6 | 🟠 Serious | `defenitions.cpp` | Захардкоженный ID |
| 7 | 🟠 Serious | `sim.cpp` | Закомментированный reconnect |
| 8 | 🟠 Serious | `main.cpp` | Нет TTL для пакетов в flash |
| 9 | 🟠 Serious | `main.cpp` | Heap fragmentation (String logBuf) |
| 10 | 🟠 Serious | `defenitions.cpp` | Невалидная калибровка по умолчанию |
| 11 | 🟠 Serious | `sys.cpp` | Непоследовательное управление питанием |
| 12 | 🟡 Medium | headers | `and` вместо `&&` в `#if` |
| 13 | 🟡 Medium | `sys.cpp` | Дублирование CRC |
| 14 | 🟡 Medium | `main.cpp` | Нет валидации Modbus-ответа |
| 15 | 🟡 Medium | `sys.cpp` | Отсутствие `volatile` |
| 16 | 🟡 Medium | `FlashStack.cpp` | Нет версии формата |
| 17 | 🟡 Medium | `main.cpp` | Нет индикации "сенсор не ответил" |
| 18 | 🟡 Medium | `main.cpp` | SIM не завершается перед сном |
| 19 | 🟡 Medium | multiple | Magic numbers 198/200 |
| 20 | 🟡 Medium | `rs.cpp` | Блокирующий таймаут без WDT |

---

## 🛠 Приоритет исправления

**Немедленно (перед следующим релизом):**
1. Утечка памяти SIM (#1) — 2 строки
2. Watchdog в длинных циклах (#5) — добавить `esp_task_wdt_reset()`
3. Buffer overflow (#4) — привязать размер к константе
4. SIM end() перед сном (#18) — 1 строка

**Следующий спринт:**
5. FlashStack checksum (#2)
6. Переместить ID в Preferences (#6)
7. Раскомментировать reconnect (#7)
8. Заменить String logBuf на кольцевой буфер (#9)

**Плановые улучшения:**
9. Всё остальное (#12-20)

Хотите, чтобы я начал исправлять критические проблемы?
