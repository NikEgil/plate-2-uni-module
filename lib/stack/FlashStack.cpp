#include "FlashStack.h"
#include <Arduino.h>
#include <cstring>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <esp_task_wdt.h>

// ------------------------------------------------------------------
// Конфигурация
// ------------------------------------------------------------------
#define SECTOR_SIZE       4096
#define REC_PER_SECTOR    (SECTOR_SIZE / sizeof(record_t))   // 19
#define TOTAL_SECTORS     736
#define MAX_RECORDS       6000

#pragma pack(push, 1)
typedef struct {
    uint32_t seq;
    uint16_t length;
    uint8_t  data[FlashStack::DATA_LENGTH];
    uint16_t crc;
} record_t;
#pragma pack(pop)

typedef struct {
    uint16_t head_sector;
    uint16_t head_offset;
    uint16_t tail_sector;
    uint16_t tail_offset;
    uint32_t seq_next;
    uint16_t rec_count[TOTAL_SECTORS];
    uint16_t free_list[TOTAL_SECTORS];
    uint16_t free_head, free_tail, free_count;
    uint16_t busy_list[TOTAL_SECTORS];
    uint16_t busy_head, busy_tail, busy_count;
    uint32_t flash_total;
    uint32_t magic;
} flash_state_t;

static volatile RTC_NOINIT_ATTR flash_state_t fstate;
static const esp_partition_t *flash_partition = nullptr;

// ------------------------------------------------------------------
// Вспомогательные функции
// ------------------------------------------------------------------
static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else              crc <<= 1;
        }
    }
    return crc;
}

static void fill_record_crc(record_t *rec) {
    // CRC вычисляется по seq, length и data (все поля до crc)
    rec->crc = crc16_ccitt((const uint8_t *)rec, offsetof(record_t, crc));
}

static bool check_record_crc(const record_t *rec) {
    uint16_t computed = crc16_ccitt((const uint8_t *)rec, offsetof(record_t, crc));
    return computed == rec->crc;
}

static void init_flash_state() {
    memset((void*)&fstate, 0, sizeof(fstate));
    fstate.head_sector = 0xFFFF;
    fstate.tail_sector = 0xFFFF;
    fstate.seq_next = 0;
    
    // Инициализация кольцевой очереди свободных секторов
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
}

// ------------------------------------------------------------------
// Восстановление состояния после сбоя RTC
// ------------------------------------------------------------------
static bool recover_state() {
    Serial.println("Recovering flash state...");
    static struct {
        uint32_t seq;
        uint16_t sector;
        uint16_t offset;
    } entries[MAX_RECORDS];
    size_t entry_count = 0;
    bool all_empty = true;

    for (int sec = 0; sec < TOTAL_SECTORS; sec++) {
        if ((sec & 0x7) == 0) {
            esp_task_wdt_reset();
            yield();
        }
        // Читаем только полные записи в пределах сектора
        for (int slot = 0; slot < REC_PER_SECTOR; slot++) {
            if (entry_count >= MAX_RECORDS) break;
            uint32_t off = slot * sizeof(record_t);
            record_t rec;
            esp_partition_read(flash_partition, sec * SECTOR_SIZE + off, &rec, sizeof(rec));
            if (rec.seq == 0xFFFFFFFF) continue;
            all_empty = false;
            if (check_record_crc(&rec)) {
                entries[entry_count].seq = rec.seq;
                entries[entry_count].sector = sec;
                entries[entry_count].offset = off;
                entry_count++;
            }
        }
        if (entry_count >= MAX_RECORDS) break;
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

    // Сортировка вставками по seq
    for (size_t i = 1; i < entry_count; i++) {
        auto tmp = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].seq > tmp.seq) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }

    memset((void*)&fstate, 0, sizeof(fstate));
    
    // Собираем свободные сектора
    bool used[TOTAL_SECTORS] = {false};
    for (size_t k = 0; k < entry_count; k++) {
        used[entries[k].sector] = true;
    }
    fstate.free_count = 0;
    for (int i = 0; i < TOTAL_SECTORS; i++) {
        if (!used[i]) {
            fstate.free_list[fstate.free_count++] = i;
        }
    }
    fstate.free_head = 0;
    fstate.free_tail = (fstate.free_count == TOTAL_SECTORS) ? 0 : fstate.free_count;

    // Заполняем busy_list
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
    fstate.busy_tail = (fstate.busy_count == TOTAL_SECTORS) ? 0 : fstate.busy_count;

    // Голова и хвост очереди
    fstate.head_sector = entries[0].sector;
    fstate.head_offset = entries[0].offset;
    fstate.tail_sector = entries[entry_count - 1].sector;
    fstate.tail_offset = entries[entry_count - 1].offset + sizeof(record_t);
    if (fstate.tail_offset >= SECTOR_SIZE) {
        fstate.tail_sector = 0xFFFF;
        fstate.tail_offset = 0;
    }

    fstate.flash_total = entry_count;
    fstate.seq_next = entries[entry_count - 1].seq + 1;
    fstate.magic = 0xBEEFFEED;

    Serial.printf("Recovered %u records\n", fstate.flash_total);
    return true;
}

// ------------------------------------------------------------------
// Методы класса
// ------------------------------------------------------------------
FlashStack::FlashStack() {}

bool FlashStack::begin() {
    flash_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "flashbuf");
    if (!flash_partition) {
        Serial.println("ERROR: flashbuf partition not found!");
        return false;
    }

    if (fstate.magic != 0xBEEFFEED ||
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

bool FlashStack::write(const uint8_t *data) {
    if (!flash_partition) return false;

    size_t total = fstate.flash_total;
    if (total >= MAX_RECORDS) {
        if (fstate.head_sector == 0xFFFF) {
            fstate.flash_total = 0;
            return false;
        }
        fstate.rec_count[fstate.head_sector]--;
        fstate.flash_total--;

        fstate.head_offset += sizeof(record_t);
        if (fstate.rec_count[fstate.head_sector] == 0) {
            // Стираем сектор и перемещаем в свободные
            esp_partition_erase_range(flash_partition,
                                      fstate.head_sector * SECTOR_SIZE,
                                      SECTOR_SIZE);
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
    }

    record_t rec;
    rec.seq = fstate.seq_next++;
    rec.length = DATA_LENGTH;
    memcpy(rec.data, data, DATA_LENGTH);
    fill_record_crc(&rec);

    if (fstate.tail_sector == 0xFFFF ||
        (fstate.tail_offset + sizeof(record_t) > SECTOR_SIZE)) {
        if (fstate.free_count == 0) return false;
        uint16_t new_sec = fstate.free_list[fstate.free_head];
        fstate.free_head = (fstate.free_head + 1) % TOTAL_SECTORS;
        fstate.free_count--;

        fstate.tail_sector = new_sec;
        fstate.tail_offset = 0;

        fstate.busy_list[fstate.busy_tail] = new_sec;
        fstate.busy_tail = (fstate.busy_tail + 1) % TOTAL_SECTORS;
        fstate.busy_count++;

        if (fstate.flash_total == 0) {
            fstate.head_sector = new_sec;
            fstate.head_offset = 0;
            fstate.busy_head = (fstate.busy_tail == 0) ? TOTAL_SECTORS - 1 : fstate.busy_tail - 1;
        }
    }

    uint32_t addr = fstate.tail_sector * SECTOR_SIZE + fstate.tail_offset;
    esp_partition_write(flash_partition, addr, &rec, sizeof(record_t));

    fstate.rec_count[fstate.tail_sector]++;
    fstate.flash_total++;
    fstate.tail_offset += sizeof(record_t);
    return true;
}

bool FlashStack::read(uint8_t *buffer) {
    if (!flash_partition) return false;
    if (fstate.flash_total == 0 || fstate.head_sector == 0xFFFF) return false;

    uint32_t addr = fstate.head_sector * SECTOR_SIZE + fstate.head_offset;
    record_t rec;
    esp_partition_read(flash_partition, addr, &rec, sizeof(rec));

    if (!check_record_crc(&rec)) {
        // Повреждённая запись – удаляем
        fstate.rec_count[fstate.head_sector]--;
        fstate.flash_total--;

        fstate.head_offset += sizeof(record_t);
        if (fstate.rec_count[fstate.head_sector] == 0) {
            esp_partition_erase_range(flash_partition,
                                      fstate.head_sector * SECTOR_SIZE,
                                      SECTOR_SIZE);
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
        return false;
    }

    memcpy(buffer, rec.data, DATA_LENGTH);

    fstate.rec_count[fstate.head_sector]--;
    fstate.flash_total--;

    fstate.head_offset += sizeof(record_t);
    if (fstate.rec_count[fstate.head_sector] == 0) {
        esp_partition_erase_range(flash_partition,
                                  fstate.head_sector * SECTOR_SIZE,
                                  SECTOR_SIZE);
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
    return true;
}

size_t FlashStack::count() {
    return fstate.flash_total;
}

bool FlashStack::clear() {
    for (int i = 0; i < TOTAL_SECTORS; i++) {
        esp_partition_erase_range(flash_partition, i * SECTOR_SIZE, SECTOR_SIZE);
    }
    init_flash_state();
    return true;
}