#pragma once
#include <Arduino.h>
#include <LittleFS.h>

#define DATA_FILE   "/stack.dat"
#define META_FILE   "/stack.meta"
#define RECORD_SIZE 198   // <-- БЫЛО 200, СТАЛО 198
#define MAX_DATA_MB 1.9
#define MAX_BYTES   ((size_t)(MAX_DATA_MB * 1024 * 1024))
#define MAX_RECORDS (MAX_BYTES / RECORD_SIZE)

struct StackMeta {
    uint32_t magic;
    uint32_t head;
    uint32_t tail;
    uint16_t count;
};

class FlashStack {
public:
    bool begin();
    bool push(const uint8_t* data);
    bool pop(uint8_t* outData);
    uint16_t count();
    void clear();

private:
    StackMeta meta;
    bool loadMeta();
    bool initFresh();
    bool saveMeta();
};