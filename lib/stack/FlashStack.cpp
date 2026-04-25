#include "FlashStack.h"

bool FlashStack::begin() {
    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed. Formatting...");
        LittleFS.format();
        if (!LittleFS.begin()) return false;
    }
    return loadMeta();
}

bool FlashStack::loadMeta() {
    if (LittleFS.exists(META_FILE)) {
        File f = LittleFS.open(META_FILE, FILE_READ);
        if (!f) return initFresh();
        
        size_t bytesRead = f.read((uint8_t*)&meta, sizeof(meta));
        f.close();
        
        if (bytesRead != sizeof(meta)) return initFresh();
        return (meta.magic == 0x53544B30);
    }
    return initFresh();
}

bool FlashStack::initFresh() {
    meta.magic = 0x53544B30;
    meta.head  = 0;
    meta.tail  = 0;
    meta.count = 0;
    return saveMeta();
}

bool FlashStack::saveMeta() {
    File f = LittleFS.open(META_FILE, FILE_WRITE);
    if (!f) return false;
    
    size_t w = f.write((const uint8_t*)&meta, sizeof(meta));
    f.close();
    return (w == sizeof(meta));
}

bool FlashStack::push(const uint8_t* data) {
    if (meta.count >= MAX_RECORDS) return false;
    
    // ⚠️ FILE_WRITE обрезает файл! Используем FILE_APPEND + seek
    File f = LittleFS.open(DATA_FILE, FILE_APPEND);
    if (!f) return false;
    
    // Если файл только что создан (пустой), его размер 0
    // seek() корректно сработает даже если файл меньше meta.tail
    f.seek(meta.tail);
    size_t w = f.write(data, RECORD_SIZE);
    f.close();
    
    yield();

    if (w == RECORD_SIZE) {
        meta.tail = (meta.tail + RECORD_SIZE) % MAX_BYTES;
        meta.count++;
        saveMeta();
        yield();
        return true;
    }
    return false;
}

bool FlashStack::pop(uint8_t* outData) {
    if (meta.count == 0) return false;
    
    File f = LittleFS.open(DATA_FILE, FILE_READ);
    if (!f) return false;
    
    f.seek(meta.head);
    size_t r = f.read(outData, RECORD_SIZE);
    f.close();
    
    yield();

    if (r == RECORD_SIZE) {
        meta.head = (meta.head + RECORD_SIZE) % MAX_BYTES;
        meta.count--;
        if (meta.count == 0) {
            meta.head = 0;
            meta.tail = 0;
        }
        saveMeta();
        yield();
        return true;
    }
    return false;
}

uint16_t FlashStack::count() {
    return meta.count;
}

void FlashStack::clear() {
    LittleFS.remove(DATA_FILE);
    initFresh();
}