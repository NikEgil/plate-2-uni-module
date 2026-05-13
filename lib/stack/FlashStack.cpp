#include "FlashStack.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <esp_attr.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <esp_task_wdt.h> // в начало файла
// ------------------------------------------------------------------
// Конфигурация и структуры
// ------------------------------------------------------------------
#define RTC_BUF_CAP 4
#define SECTOR_SIZE 4096
#define REC_PER_SECTOR (SECTOR_SIZE / sizeof(record_t)) // 19
#define TOTAL_SECTORS 736 // покрывает 6000 записей с запасом

#pragma pack(push, 1)
typedef struct {
    uint32_t seq;
    uint16_t length;
    uint16_t crc;
    uint8_t data[FlashStack::DATA_LENGTH];
} record_t;
#pragma pack(pop)

// RTC-буфер (сохраняется в глубоком сне)
typedef struct {
    record_t entries[RTC_BUF_CAP];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint32_t magic; // 0xDEADBEEF
} rtc_buf_t;

// Состояние флеш-буфера (хранится в RTC-памяти)
typedef struct {
    uint16_t head_sector; // индекс сектора, где находится самая старая запись
                          // (0xFFFF = нет)
    uint16_t head_offset;
    uint16_t tail_sector;
    uint16_t tail_offset;
    uint32_t seq_next;
    uint16_t rec_count[TOTAL_SECTORS]; // кол-во невычитанных записей в секторе
    uint16_t free_list[TOTAL_SECTORS];
    uint16_t free_head;
    uint16_t free_tail;
    uint16_t free_count;
    uint32_t flash_total; // общее кол-во записей во флеш-буфере
    uint32_t magic;       // 0xBEEFFEED
} flash_state_t;

// ------------------------------------------------------------------
// Глобальные переменные в RTCNOINIT секции
// ------------------------------------------------------------------
static RTC_NOINIT_ATTR rtc_buf_t rtc_buf;
static RTC_NOINIT_ATTR flash_state_t fstate;

// Указатель на раздел, получаемый в begin()
static const esp_partition_t *flash_partition = nullptr;

// ------------------------------------------------------------------
// Вспомогательные функции
// ------------------------------------------------------------------
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

static void fill_record_crc(record_t *rec) {
    rec->crc = crc16_ccitt((const uint8_t *)rec, offsetof(record_t, crc));
}

static bool check_record_crc(const record_t *rec) {
    uint16_t computed =
        crc16_ccitt((const uint8_t *)rec, offsetof(record_t, crc));
    return computed == rec->crc;
}

// ------------------------------------------------------------------
// Полное восстановление состояния флеш-буфера после пропадания питания
// ------------------------------------------------------------------
static void init_flash_state() {
    memset(&fstate, 0, sizeof(fstate));
    fstate.head_sector = 0xFFFF;
    fstate.tail_sector = 0xFFFF;
    fstate.seq_next = 0;
    fstate.free_head = 0;
    fstate.free_tail = TOTAL_SECTORS;
    fstate.free_count = TOTAL_SECTORS;
    for (int i = 0; i < TOTAL_SECTORS; i++) {
        fstate.free_list[i] = i;
    }
    fstate.flash_total = 0;
    fstate.magic = 0xBEEFFEED;
}

// ------------------------------------------------------------------
// Методы класса
// ------------------------------------------------------------------
FlashStack::FlashStack() {}

bool FlashStack::begin() {
    Serial.println("begin started");
    flash_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY, // ищем по имени, игнорируя подтип
        "flashbuf");
    if (!flash_partition) {
        Serial.println("ERROR: flashbuf partition not found!");
        return false;
    }
    Serial.printf("flashbuf partition found at offset 0x%X, size 0x%X\n",
                  flash_partition->address, flash_partition->size);

    // Инициализация RTC-буфера
    if (rtc_buf.magic != 0xDEADBEEF) {
        memset(&rtc_buf, 0, sizeof(rtc_buf));
        rtc_buf.magic = 0xDEADBEEF;
    }

    // Инициализация состояния флеш-буфера (без сканирования)
    if (fstate.magic != 0xBEEFFEED) {
        Serial.println("Initializing flash state (blank)");
        init_flash_state();
    }
    Serial.println("begin finished OK");
    return true;
}

bool FlashStack::write(const uint8_t *data) {
    if (!flash_partition)
        return false;

    // Если достигнут лимит, предварительно удаляем одну самую старую запись
    size_t total = rtc_buf.count + fstate.flash_total;
    if (total >= MAX_RECORDS) {
        // discard oldest
        if (fstate.flash_total > 0) {
            record_t dummy;
            // чтение из флеш с удалением
            if (fstate.head_sector != 0xFFFF) {
                uint32_t addr =
                    fstate.head_sector * SECTOR_SIZE + fstate.head_offset;
                esp_partition_read(flash_partition, addr, &dummy,
                                   sizeof(record_t));
                fstate.rec_count[fstate.head_sector]--;
                fstate.flash_total--;

                // сдвиг head
                fstate.head_offset += sizeof(record_t);
                if (fstate.rec_count[fstate.head_sector] == 0) {
                    // сектор полностью освобождён – стираем и в free_list
                    esp_partition_erase_range(flash_partition,
                                              fstate.head_sector * SECTOR_SIZE,
                                              SECTOR_SIZE);
                    fstate.free_list[fstate.free_tail] = fstate.head_sector;
                    fstate.free_tail = (fstate.free_tail + 1) % TOTAL_SECTORS;
                    fstate.free_count++;

                    // Найти следующий занятый сектор по порядку seq
                    // Поскольку все секторы идут в порядке seq, а мы знаем
                    // индексы, надо искать следующий. Но у нас нет прямой
                    // связи. Быстрое решение: просканируем массив занятых
                    // секторов. Мы храним их упорядоченно в RTC? Нет. Для
                    // простоты можно сохранять порядок в отдельном массиве.
                    // Сейчас для упрощения будем искать минимальный seq из
                    // rec_count > 0.
                    uint32_t min_seq = UINT32_MAX;
                    uint16_t next_sector = 0xFFFF;
                    uint16_t next_offset = 0;
                    for (int s = 0; s < TOTAL_SECTORS; s++) {
                        if (fstate.rec_count[s] > 0) {
                            // найти первую запись в секторе
                            record_t r;
                            for (int i = 0; i < REC_PER_SECTOR; i++) {
                                esp_partition_read(flash_partition,
                                                   s * SECTOR_SIZE +
                                                       i * sizeof(record_t),
                                                   &r, sizeof(r));
                                if (r.seq != 0xFFFFFFFF &&
                                    check_record_crc(&r)) {
                                    if (r.seq < min_seq) {
                                        min_seq = r.seq;
                                        next_sector = s;
                                        next_offset = i * sizeof(record_t);
                                    }
                                    break; // важна первая запись с минимальным
                                           // seq в секторе
                                }
                            }
                        }
                    }
                    fstate.head_sector = next_sector;
                    fstate.head_offset = next_offset;
                }
            }
        } else if (rtc_buf.count > 0) {
            rtc_buf.head = (rtc_buf.head + 1) % RTC_BUF_CAP;
            rtc_buf.count--;
        }
    }

    // Запись в RTC-буфер
    record_t rec;
    rec.seq = 0; // в RTC seq не используется, но заполним для uniform
    rec.length = DATA_LENGTH;
    memcpy(rec.data, data, DATA_LENGTH);
    fill_record_crc(&rec);

    if (rtc_buf.count < RTC_BUF_CAP) {
        rtc_buf.entries[rtc_buf.tail] = rec;
        rtc_buf.tail = (rtc_buf.tail + 1) % RTC_BUF_CAP;
        rtc_buf.count++;
    } else {
        // RTC буфер полон, выталкиваем самую старую запись во флеш
        record_t old_rec = rtc_buf.entries[rtc_buf.head];
        rtc_buf.head = (rtc_buf.head + 1) % RTC_BUF_CAP;
        rtc_buf.count--;

        // Проверяем, нужен ли новый сектор ДО записи (чтобы не выйти за границу)
        if (fstate.tail_sector == 0xFFFF ||
            (fstate.tail_offset + sizeof(record_t) > SECTOR_SIZE)) {
            if (fstate.free_count == 0) {
                // нет свободных секторов – такого не должно быть при правильном расчёте
                return false;
            }
            uint16_t new_sec = fstate.free_list[fstate.free_head];
            fstate.free_head = (fstate.free_head + 1) % TOTAL_SECTORS;
            fstate.free_count--;
            fstate.tail_sector = new_sec;
            fstate.tail_offset = 0;
        }

        bool is_first_flash_record = (fstate.flash_total == 0);

        old_rec.seq = fstate.seq_next++;
        fill_record_crc(&old_rec);

        uint32_t addr = fstate.tail_sector * SECTOR_SIZE + fstate.tail_offset;
        esp_partition_write(flash_partition, addr, &old_rec, sizeof(record_t));
        fstate.rec_count[fstate.tail_sector]++;
        fstate.flash_total++;

        if (is_first_flash_record) {
            fstate.head_sector = fstate.tail_sector;
            fstate.head_offset = fstate.tail_offset;
        }

        fstate.tail_offset += sizeof(record_t);

        // Теперь записываем новую запись в RTC
        rtc_buf.entries[rtc_buf.tail] = rec;
        rtc_buf.tail = (rtc_buf.tail + 1) % RTC_BUF_CAP;
        rtc_buf.count++;
    }
    return true;
}

bool FlashStack::read(uint8_t *buffer) {
    if (!flash_partition)
        return false;

    // Сначала пробуем прочитать из флеш-буфера
    if (fstate.flash_total > 0 && fstate.head_sector != 0xFFFF) {
        record_t rec;
        uint32_t addr = fstate.head_sector * SECTOR_SIZE + fstate.head_offset;
        esp_partition_read(flash_partition, addr, &rec, sizeof(rec));

        if (!check_record_crc(&rec)) {
            // данные повреждены, аварийно восстанавливаем?
            return false;
        }
        memcpy(buffer, rec.data, DATA_LENGTH);

        // Удаляем запись из флеш-буфера
        fstate.rec_count[fstate.head_sector]--;
        fstate.flash_total--;

        fstate.head_offset += sizeof(record_t);
        if (fstate.rec_count[fstate.head_sector] == 0) {
            // сектор пуст, стереть и в free_list
            esp_partition_erase_range(
                flash_partition, fstate.head_sector * SECTOR_SIZE, SECTOR_SIZE);
            fstate.free_list[fstate.free_tail] = fstate.head_sector;
            fstate.free_tail = (fstate.free_tail + 1) % TOTAL_SECTORS;
            fstate.free_count++;

            // Найти следующий сектор с записями (с минимальным seq)
            uint32_t min_seq = UINT32_MAX;
            uint16_t next_sector = 0xFFFF;
            uint16_t next_offset = 0;
            for (int s = 0; s < TOTAL_SECTORS; s++) {
                if (fstate.rec_count[s] > 0) {
                    record_t r;
                    for (int i = 0; i < REC_PER_SECTOR; i++) {
                        esp_partition_read(flash_partition,
                                           s * SECTOR_SIZE +
                                               i * sizeof(record_t),
                                           &r, sizeof(r));
                        if (r.seq != 0xFFFFFFFF && check_record_crc(&r)) {
                            if (r.seq < min_seq) {
                                min_seq = r.seq;
                                next_sector = s;
                                next_offset = i * sizeof(record_t);
                            }
                            break;
                        }
                    }
                }
            }
            fstate.head_sector = next_sector;
            fstate.head_offset = next_offset;
        }
        return true;
    }

    // Если флеш пуст, читаем из RTC
    if (rtc_buf.count > 0) {
        record_t rec = rtc_buf.entries[rtc_buf.head];
        memcpy(buffer, rec.data, DATA_LENGTH);
        rtc_buf.head = (rtc_buf.head + 1) % RTC_BUF_CAP;
        rtc_buf.count--;
        return true;
    }

    return false; // буфер пуст
}

size_t FlashStack::count() { return rtc_buf.count + fstate.flash_total; }

bool FlashStack::clear() {
    // Очистить RTC
    rtc_buf.head = rtc_buf.tail = rtc_buf.count = 0;

    // Очистить флеш: пройти по всем секторам и стереть
    for (int s = 0; s < TOTAL_SECTORS; s++) {
        esp_partition_erase_range(flash_partition, s * SECTOR_SIZE,
                                  SECTOR_SIZE);
    }

    // Сбросить состояние флеш-буфера
    fstate.head_sector = 0xFFFF;
    fstate.head_offset = 0;
    fstate.tail_sector = 0xFFFF;
    fstate.tail_offset = 0;
    fstate.seq_next = 0;
    memset(fstate.rec_count, 0, sizeof(fstate.rec_count));
    fstate.free_head = 0;
    fstate.free_tail = TOTAL_SECTORS;
    fstate.free_count = TOTAL_SECTORS;
    for (int i = 0; i < TOTAL_SECTORS; i++) {
        fstate.free_list[i] = i;
    }
    fstate.flash_total = 0;
    fstate.magic =
        0xBEEFFEED; // сохраняем magic, чтобы не запускалось восстановление
    return true;
}