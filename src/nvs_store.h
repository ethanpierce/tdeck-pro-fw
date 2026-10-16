#pragma once
#include <nvs.h>
#include <cstddef>

// The same ESP-IDF blob representation used by the previous firmware. Opening
// or reading a damaged/newer partition never erases it or formats the device.
class NvsStore {
public:
    NvsStore() = default;
    ~NvsStore() { if (opened_) nvs_close(handle_); }
    NvsStore(const NvsStore&) = delete;
    NvsStore& operator=(const NvsStore&) = delete;
    bool begin(const char* name, bool readOnly = false) {
        if (opened_) nvs_close(handle_);
        opened_ = nvs_open(name, readOnly ? NVS_READONLY : NVS_READWRITE, &handle_) == ESP_OK;
        return opened_;
    }
    size_t getBytesLength(const char* key) const {
        size_t length = 0;
        return opened_ && nvs_get_blob(handle_, key, nullptr, &length) == ESP_OK ? length : 0;
    }
    bool contains(const char* key) const {
        size_t length=0;
        return opened_ && nvs_get_blob(handle_,key,nullptr,&length)==ESP_OK;
    }
    size_t getBytes(const char* key, void* data, size_t capacity) const {
        if (!opened_) return 0;
        size_t length = capacity;
        return nvs_get_blob(handle_, key, data, &length) == ESP_OK ? length : 0;
    }
    size_t putBytes(const char* key, const void* data, size_t length) {
        if (!opened_ || nvs_set_blob(handle_, key, data, length) != ESP_OK) return 0;
        return nvs_commit(handle_) == ESP_OK ? length : 0;
    }
private:
    nvs_handle_t handle_ = 0;
    bool opened_ = false;
};
