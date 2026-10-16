#pragma once
#include <Arduino.h>
#include <ai_protocol.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>
class AiChat {
public:
    bool begin();
    bool send(const deck::AiConfig& config,const String& prompt);
    bool poll();
    bool clear();
    bool busy=false,available=false;
    String status="Set endpoint, model and key in Settings > I";
    const std::vector<deck::AiMessage>& messages()const{return history_;}
private:
    struct Job {std::string url,key,body;deck::AiReply reply;};
    QueueHandle_t jobs_=nullptr,results_=nullptr;
    std::vector<deck::AiMessage> history_;
    std::string pending_;
    static void worker(void* self);
    static deck::AiReply request(const Job& job);
};
