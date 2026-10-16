#pragma once
#include "nvs_store.h"
#include <ai_protocol.h>
class AiSettings {
public:
    enum class Field {Endpoint,Model,Key};
    void begin();
    bool save(deck::AiConfig value);
    bool set(Field field,const std::string& text);
    const deck::AiConfig& values()const{return value_;}
    bool persistent=false;
private:
    NvsStore prefs_;
    deck::AiConfig value_{};
};

static_assert(sizeof(deck::AiConfig)==552, "Keep the existing NVS AI configuration layout");
