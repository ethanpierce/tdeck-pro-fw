#pragma once
#include <platform.h>
#include <ai_protocol.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>
class AiChat {
public:
    bool begin();
    bool send(const deck::AiConfig& config,const std::string& prompt);
    bool poll();
    bool clear();
    bool busy=false,available=false;
    std::string status="Set up your AI provider in Settings.";
    const std::vector<deck::AiMessage>& messages()const{return history_;}
private:
    struct Job {std::string url,key,body;uint32_t radioGeneration=0;bool cellular=false;deck::AiReply reply;};
    QueueHandle_t jobs_=nullptr,results_=nullptr;
    std::vector<deck::AiMessage> history_;
    std::string pending_;
    static void worker(void* self);
    static deck::AiReply request(const Job& job);
};
