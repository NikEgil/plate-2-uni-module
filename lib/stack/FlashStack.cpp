#include "FlashStack.h"
#include <Arduino.h>
#include <cstring>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <esp_task_wdt.h>

// =====================================================================
// Конфигурация
// =====================================================================
#define SECTOR_SIZE 4096 ///< Размер сектора Flash (4 КБ)
#define REC_PER_SECTOR                                                         \
    (SECTOR_SIZE / sizeof(record_t)) ///< ~19 записей на сектор
#define TOTAL_SECTORS 736            ///< Всего секторов в разделе (около 3 МБ)
#define MAX_RECORDS 6000 ///< Максимальное число записей (ограничение памяти)

// =====================================================================
// Структура записи в Flash
// =====================================================================
#pragma pack(push, 1)
/**
 * @struct record_t
 * @brief Формат одной записи, сохраняемой во Flash.
 * @details Содержит порядковый номер, длину данных (фиксированная),
 *          полезные данные и CRC-16 для контроля целостности.
 */
typedef struct {
    uint32_t seq;    ///< Порядковый номер (монотонно возрастающий)
    uint16_t length; ///< Длина данных (всегда DATA_LENGTH)
    uint8_t data[FlashStack::DATA_LENGTH]; ///< Полезная нагрузка
    uint16_t crc;                          ///< CRC-16 всей записи до этого поля
} record_t;
#pragma pack(pop)

// =====================================================================
// Структура состояния очереди (хранится в RTC_NOINIT_ATTR)
// =====================================================================
/**
 * @struct flash_state_t
 * @brief Внутреннее состояние очереди, размещаемое в RTC-памяти (не теряется
 * при глубоком сне).
 * @details Содержит указатели на голову и хвост (сектор + смещение),
 *          списки свободных и занятых секторов, счётчики и контрольную сумму.
 *          Обновляется после каждой операции и защищена CRC32.
 */
typedef struct {
    uint16_t head_sector; ///< Сектор головы (первая запись)
    uint16_t head_offset; ///< Смещение внутри сектора головы
    uint16_t tail_sector; ///< Сектор хвоста (последняя запись)
    uint16_t tail_offset; ///< Смещение внутри сектора хвоста (следующее
                          ///< свободное место)
    uint32_t seq_next; ///< Следующий порядковый номер для новой записи
    uint16_t rec_count[TOTAL_SECTORS]; ///< Количество записей в каждом секторе
    uint16_t free_list[TOTAL_SECTORS]; ///< Массив номеров свободных секторов
                                       ///< (кольцевая очередь)
    uint16_t free_head, free_tail,
        free_count;                    ///< Индексы и кол-во свободных секторов
    uint16_t busy_list[TOTAL_SECTORS]; ///< Массив номеров занятых секторов
                                       ///< (кольцевая очередь)
    uint16_t busy_head, busy_tail,
        busy_count;       ///< Индексы и кол-во занятых секторов
    uint32_t flash_total; ///< Общее количество записей в очереди
    uint32_t magic; ///< Магическое число 0xBEEFFEED для проверки валидности
    uint32_t crc32; ///< CRC32 всей структуры (кроме этого поля) для защиты от
                    ///< мусора в RTC
} flash_state_t;

// Глобальный объект состояния – размещается в RTC_NOINIT_ATTR
static volatile RTC_NOINIT_ATTR flash_state_t fstate;

// Указатель на найденный раздел Flash
static const esp_partition_t *flash_partition = nullptr;

// =====================================================================
// Вспомогательные функции
// =====================================================================

/**
 * @brief Вычисляет CRC-16/CCITT (полином 0x1021, начальное 0xFFFF).
 * @param data указатель на данные
 * @param len длина в байтах
 * @return uint16_t вычисленное CRC.
 * @note Используется для контроля целостности каждой записи.
 */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief Заполняет поле crc в записи, вычисляя CRC по всем предыдущим полям.
 * @param rec указатель на запись.
 */
static void fill_record_crc(record_t *rec) {
    rec->crc = crc16_ccitt((const uint8_t *)rec, offsetof(record_t, crc));
}

/**
 * @brief Проверяет CRC записи.
 * @param rec указатель на запись.
 * @return true если CRC совпадает, иначе false.
 */
static bool check_record_crc(const record_t *rec) {
    uint16_t computed =
        crc16_ccitt((const uint8_t *)rec, offsetof(record_t, crc));
    return computed == rec->crc;
}

// ------------------------------------------------------------------
// CRC32 для защиты RTC_NOINIT_ATTR от мусора после сбоя питания
// ------------------------------------------------------------------

/**
 * @brief Вычисляет CRC32 (алгоритм ZIP/PNG) для блока данных.
 * @param data указатель на данные
 * @param len длина
 * @return uint32_t вычисленный CRC32.
 */
static uint32_t calcCRC32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return ~crc;
}

/**
 * @brief Обновляет поле crc32 в структуре fstate.
 * @details Вычисляет CRC по всей структуре до поля crc32 и сохраняет его.
 */
static void update_state_crc() {
    size_t len = offsetof(flash_state_t, crc32);
    fstate.crc32 = calcCRC32((const void *)&fstate, len);
}

/**
 * @brief Проверяет, совпадает ли сохранённый CRC32 с вычисленным.
 * @return true если CRC совпадает, иначе false.
 */
static bool verify_state_crc() {
    size_t len = offsetof(flash_state_t, crc32);
    uint32_t expected = calcCRC32((const void *)&fstate, len);
    return fstate.crc32 == expected;
}

// =====================================================================
// Инициализация чистого состояния
// =====================================================================

/**
 * @brief Инициализирует структуру fstate для пустой очереди.
 * @details Все сектора считаются свободными, головы/хвоста нет, seq_next = 0,
 *          magic устанавливается, CRC пересчитывается.
 */
static void init_flash_state() {
    memset((void *)&fstate, 0, sizeof(fstate));
    fstate.head_sector = 0xFFFF;
    fstate.tail_sector = 0xFFFF;
    fstate.seq_next = 0;

    // Инициализация списка свободных секторов (все сектора свободны)
    fstate.free_head = 0;
    fstate.free_tail = 0;
    fstate.free_count = TOTAL_SECTORS;
    for (int i = 0; i < TOTAL_SECTORS; i++) {
        fstate.free_list[i] = i;
    }
    fstate.busy_head = 0;
    fstate.busy_tail = 0;
    fstate.busy_count = 0;

    fstate.flash_total = 0;
    fstate.magic = 0xBEEFFEED;
    update_state_crc(); // Защита RTC_NOINIT_ATTR
}

// =====================================================================
// Восстановление состояния после сбоя или при невалидном RTC
// =====================================================================

/**
 * @brief Сканирует весь раздел Flash, собирает валидные записи и
 * восстанавливает состояние.
 * @return true если восстановление успешно (всегда true, так как в крайнем
 * случае инициализирует пустое).
 * @details Считывает все сектора, проверяет CRC каждой записи, сортирует по
 * seq, строит списки свободных/занятых секторов, вычисляет голову и хвост.
 *          Используется при первом запуске или после сбоя питания.
 */
static bool recover_state() {
    Serial.println("Recovering flash state...");
    // Временный массив для хранения найденных записей (seq, sector, offset)
    static struct {
        uint32_t seq;
        uint16_t sector;
        uint16_t offset;
    } entries[MAX_RECORDS];
    size_t entry_count = 0;
    bool all_empty = true;

    // Проход по всем секторам и слотам
    for (int sec = 0; sec < TOTAL_SECTORS; sec++) {
        // Периодически сбрасываем Watchdog, чтобы не было перезагрузки
        if ((sec & 0x7) == 0) {
            esp_task_wdt_reset();
            yield();
        }
        // Читаем только полные записи в пределах сектора
        for (int slot = 0; slot < REC_PER_SECTOR; slot++) {
            if (entry_count >= MAX_RECORDS)
                break;
            uint32_t off = slot * sizeof(record_t);
            record_t rec;
            esp_partition_read(flash_partition, sec * SECTOR_SIZE + off, &rec,
                               sizeof(rec));
            if (rec.seq == 0xFFFFFFFF)
                continue; // пустое место
            all_empty = false;
            if (check_record_crc(&rec)) {
                entries[entry_count].seq = rec.seq;
                entries[entry_count].sector = sec;
                entries[entry_count].offset = off;
                entry_count++;
            }
        }
        if (entry_count >= MAX_RECORDS)
            break;
    }

    if (all_empty) {
        Serial.println("Flash partition is empty – initialising fresh state");
        init_flash_state();
        return true;
    }

    if (entry_count == 0) {
        Serial.println("No valid records found – initialising empty buffer");
        init_flash_state();
        return true;
    }

    // Сортировка вставками по возрастанию seq
    for (size_t i = 1; i < entry_count; i++) {
        auto key = entries[i];
        int j = (int)i - 1;
        while (j >= 0 && entries[j].seq > key.seq) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }

    // Построение списка свободных секторов (те, которых нет в entries)
    fstate.free_count = 0;
    for (int i = 0; i < TOTAL_SECTORS; i++) {
        bool used = false;
        for (size_t j = 0; j < entry_count; j++) {
            if (entries[j].sector == (uint16_t)i) {
                used = true;
                break;
            }
        }
        if (!used) {
            fstate.free_list[fstate.free_count++] = i;
        }
    }
    fstate.free_head = 0;
    fstate.free_tail =
        (fstate.free_count == TOTAL_SECTORS) ? 0 : fstate.free_count;

    // Построение списка занятых секторов (уникальные сектора, где есть записи)
    uint16_t last_sec = 0xFFFF;
    fstate.busy_count = 0;
    for (size_t i = 0; i < entry_count; i++) {
        if (entries[i].sector != last_sec) {
            fstate.busy_list[fstate.busy_count++] = entries[i].sector;
            last_sec = entries[i].sector;
        }
        fstate.rec_count[entries[i].sector]++;
    }
    fstate.busy_head = 0;
    fstate.busy_tail =
        (fstate.busy_count == TOTAL_SECTORS) ? 0 : fstate.busy_count;

    // Голова – самая старая запись (первая по seq)
    fstate.head_sector = entries[0].sector;
    fstate.head_offset = entries[0].offset;
    // Хвост – самая новая запись (последняя по seq) + размер записи
    fstate.tail_sector = entries[entry_count - 1].sector;
    fstate.tail_offset = entries[entry_count - 1].offset + sizeof(record_t);
    if (fstate.tail_offset >= SECTOR_SIZE) {
        fstate.tail_sector = 0xFFFF;
        fstate.tail_offset = 0;
    }

    fstate.flash_total = entry_count;
    fstate.seq_next = entries[entry_count - 1].seq + 1;
    fstate.magic = 0xBEEFFEED;
    update_state_crc(); // Защита RTC_NOINIT_ATTR

    Serial.printf("Recovered %u records\n", fstate.flash_total);
    return true;
}

// =====================================================================
// Методы класса FlashStack
// =====================================================================

FlashStack::FlashStack() {
    // Конструктор пустой, инициализация в begin()
}

/**
 * @brief Инициализирует драйвер: находит раздел "flashbuf" и восстанавливает
 * состояние.
 * @return true если раздел найден и состояние валидно (или успешно
 * восстановлено).
 * @details Проверяет magic и CRC структуры fstate. Если они не совпадают,
 * вызывает recover_state(). После этого структура готова к использованию.
 */
bool FlashStack::begin() {
    flash_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "flashbuf");
    if (!flash_partition) {
        Serial.println("ERROR: flashbuf partition not found!");
        return false;
    }

    // Проверяем валидность состояния в RTC_NOINIT_ATTR
    if (fstate.magic != 0xBEEFFEED || !verify_state_crc() ||
        fstate.flash_total > MAX_RECORDS ||
        fstate.free_count + fstate.busy_count != TOTAL_SECTORS) {
        Serial.println("Invalid state – attempting recovery");
        if (!recover_state()) {
            init_flash_state();
        }
    }
    Serial.println("FlashStack ready");
    return true;
}

/**
 * @brief Добавляет новую запись в конец очереди.
 * @param data указатель на DATA_LENGTH байт.
 * @return true если запись успешно добавлена, иначе false (например, нет
 * свободных секторов).
 * @details Если очередь переполнена (>= MAX_RECORDS), удаляет самую старую
 * запись (голову) перед добавлением новой. Запись записывается в хвостовой
 * сектор, при необходимости выделяется новый сектор из списка свободных. После
 * записи обновляется состояние.
 */
bool FlashStack::write(const uint8_t *data) {
    if (!flash_partition)
        return false;

    size_t total = fstate.flash_total;
    // Если достигнут лимит записей – удаляем самую старую
    if (total >= MAX_RECORDS) {
        if (fstate.head_sector == 0xFFFF) {
            fstate.flash_total = 0;
            return false;
        }
        // Уменьшаем счётчик записей в головном секторе
        fstate.rec_count[fstate.head_sector]--;
        fstate.flash_total--;

        // Смещаем голову на следующую запись
        fstate.head_offset += sizeof(record_t);
        // Если сектор опустел – освобождаем его
        if (fstate.rec_count[fstate.head_sector] == 0) {
            esp_partition_erase_range(
                flash_partition, fstate.head_sector * SECTOR_SIZE, SECTOR_SIZE);
            // Перемещаем сектор из busy в free
            fstate.free_list[fstate.free_tail] = fstate.head_sector;
            fstate.free_tail = (fstate.free_tail + 1) % TOTAL_SECTORS;
            fstate.free_count++;

            fstate.busy_head = (fstate.busy_head + 1) % TOTAL_SECTORS;
            fstate.busy_count--;

            // Если есть ещё занятые сектора – обновляем голову
            if (fstate.busy_count > 0) {
                fstate.head_sector = fstate.busy_list[fstate.busy_head];
                fstate.head_offset = 0;
            } else {
                // Очередь стала пустой
                fstate.head_sector = 0xFFFF;
                fstate.head_offset = 0;
                fstate.tail_sector = 0xFFFF;
                fstate.tail_offset = 0;
            }
        }
    }

    // Формируем новую запись
    record_t rec;
    rec.seq = fstate.seq_next++;
    rec.length = DATA_LENGTH;
    memcpy(rec.data, data, DATA_LENGTH);
    fill_record_crc(&rec);

    // Если хвост отсутствует (очередь пуста) – берём свежий сектор
    if (fstate.tail_sector == 0xFFFF) {
        if (fstate.free_count == 0) {
            Serial.println("No free sectors!");
            return false;
        }
        uint16_t new_sec = fstate.free_list[fstate.free_head];
        fstate.free_head = (fstate.free_head + 1) % TOTAL_SECTORS;
        fstate.free_count--;

        // Стираем сектор перед записью
        esp_partition_erase_range(flash_partition, new_sec * SECTOR_SIZE,
                                  SECTOR_SIZE);

        fstate.tail_sector = new_sec;
        fstate.tail_offset = 0;
        // Добавляем сектор в список занятых
        fstate.busy_list[fstate.busy_tail] = new_sec;
        fstate.busy_tail = (fstate.busy_tail + 1) % TOTAL_SECTORS;
        fstate.busy_count++;

        // Если это первая запись, то голова тоже указывает на этот сектор
        if (fstate.flash_total == 0) {
            fstate.head_sector = new_sec;
            fstate.head_offset = 0;
            fstate.busy_head = (fstate.busy_tail == 0) ? TOTAL_SECTORS - 1
                                                       : fstate.busy_tail - 1;
        }
    }

    // Запись в Flash
    uint32_t addr = fstate.tail_sector * SECTOR_SIZE + fstate.tail_offset;
    esp_partition_write(flash_partition, addr, &rec, sizeof(record_t));

    // Обновляем счётчики
    fstate.rec_count[fstate.tail_sector]++;
    fstate.flash_total++;
    fstate.tail_offset += sizeof(record_t);
    update_state_crc(); // Сохраняем состояние в RTC
    return true;
}

/**
 * @brief Читает самую старую запись из головы очереди и удаляет её.
 * @param buffer буфер размером DATA_LENGTH для приёма данных.
 * @return true если запись прочитана и удалена, false если очередь пуста или
 * CRC не совпал.
 * @details Если CRC не совпадает, запись считается повреждённой и удаляется
 * (пропускается). После чтения сектор может освободиться, если в нём больше нет
 * записей.
 */
bool FlashStack::read(uint8_t *buffer) {
    if (!flash_partition)
        return false;
    if (fstate.flash_total == 0 || fstate.head_sector == 0xFFFF)
        return false;

    // Читаем запись из головы
    uint32_t addr = fstate.head_sector * SECTOR_SIZE + fstate.head_offset;
    record_t rec;
    esp_partition_read(flash_partition, addr, &rec, sizeof(rec));

    // Проверяем CRC
    if (!check_record_crc(&rec)) {
        // Повреждённая запись – удаляем её и возвращаем false
        fstate.rec_count[fstate.head_sector]--;
        fstate.flash_total--;

        fstate.head_offset += sizeof(record_t);
        if (fstate.rec_count[fstate.head_sector] == 0) {
            // Освобождаем сектор
            esp_partition_erase_range(
                flash_partition, fstate.head_sector * SECTOR_SIZE, SECTOR_SIZE);
            fstate.free_list[fstate.free_tail] = fstate.head_sector;
            fstate.free_tail = (fstate.free_tail + 1) % TOTAL_SECTORS;
            fstate.free_count++;

            fstate.busy_head = (fstate.busy_head + 1) % TOTAL_SECTORS;
            fstate.busy_count--;

            if (fstate.busy_count > 0) {
                fstate.head_sector = fstate.busy_list[fstate.busy_head];
                fstate.head_offset = 0;
            } else {
                fstate.head_sector = 0xFFFF;
                fstate.head_offset = 0;
                fstate.tail_sector = 0xFFFF;
                fstate.tail_offset = 0;
            }
        }
        update_state_crc();
        return false;
    }

    // Копируем данные
    memcpy(buffer, rec.data, DATA_LENGTH);

    // Удаляем прочитанную запись из очереди
    fstate.rec_count[fstate.head_sector]--;
    fstate.flash_total--;

    fstate.head_offset += sizeof(record_t);
    if (fstate.rec_count[fstate.head_sector] == 0) {
        // Освобождаем сектор
        esp_partition_erase_range(
            flash_partition, fstate.head_sector * SECTOR_SIZE, SECTOR_SIZE);
        fstate.free_list[fstate.free_tail] = fstate.head_sector;
        fstate.free_tail = (fstate.free_tail + 1) % TOTAL_SECTORS;
        fstate.free_count++;

        fstate.busy_head = (fstate.busy_head + 1) % TOTAL_SECTORS;
        fstate.busy_count--;

        if (fstate.busy_count > 0) {
            fstate.head_sector = fstate.busy_list[fstate.busy_head];
            fstate.head_offset = 0;
        } else {
            fstate.head_sector = 0xFFFF;
            fstate.head_offset = 0;
            fstate.tail_sector = 0xFFFF;
            fstate.tail_offset = 0;
        }
    }
    update_state_crc();
    return true;
}

/**
 * @brief Возвращает текущее количество записей в очереди.
 * @return size_t число записей.
 */
size_t FlashStack::count() { return fstate.flash_total; }

/**
 * @brief Полностью очищает очередь: стирает все сектора и сбрасывает состояние.
 * @return true если успешно, иначе false.
 * @note После очистки все данные теряются, очередь становится пустой.
 */
bool FlashStack::clear() {
    if (!flash_partition)
        return false;

    // Стираем все сектора в разделе
    for (int sec = 0; sec < TOTAL_SECTORS; sec++) {
        esp_partition_erase_range(flash_partition, sec * SECTOR_SIZE,
                                  SECTOR_SIZE);
        if ((sec & 0x3F) == 0) {
            esp_task_wdt_reset();
            yield();
        }
    }
    // Инициализируем пустое состояние
    init_flash_state(); // уже включает update_state_crc()
    Serial.println("FlashStack cleared");
    return true;
}