#include "ai_chat.h"
#include <WiFi.h>
#include <esp_http_client.h>
#include <ctime>
#include <new>
// Arduino ships a different esp_crt_bundle.h that shadows the SDK header.
// Use ESP-IDF's entry point and its embedded CA bundle, not Arduino's unset bundle.
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);
namespace {
constexpr uint32_t RequestMillis=45000;
deck::AiReply failure(const char* text){deck::AiReply reply;reply.error=text;return reply;}
}
bool AiChat::begin() {
    jobs_=xQueueCreate(1,sizeof(Job*));results_=xQueueCreate(1,sizeof(Job*));
    if(!jobs_ || !results_) {
        if(jobs_)vQueueDelete(jobs_);
        if(results_)vQueueDelete(results_);
        jobs_=results_=nullptr;status="AI request queue unavailable";return false;
    }
    available=xTaskCreatePinnedToCore(worker,"ai-https",12288,this,1,nullptr,0)==pdPASS;
    if(!available){vQueueDelete(jobs_);vQueueDelete(results_);jobs_=results_=nullptr;status="AI worker unavailable";}
    return available;
}
bool AiChat::send(const deck::AiConfig& config,const String& prompt) {
    if(busy){status="Wait for the current AI request";return false;}
    if(!available){status="AI worker unavailable";return false;}
    if(WiFi.status()!=WL_CONNECTED){status="Connect Wi-Fi first: Settings > W";return false;}
    if(!deck::validAiConfig(config) || !config.model[0]){status="Set an HTTPS endpoint and model in Settings > I";return false;}
    const auto url=deck::aiCompletionUrl(config.endpoint);
    if(!config.key[0] && url.find("https://api.openai.com/")==0){status="Add your API key in Settings > I";return false;}
    auto* job=new(std::nothrow) Job;
    if(!job){status="Not enough memory for AI request";return false;}
    job->url=url;
    const bool openAiProfile=url=="https://api.openai.com/v1/chat/completions" || url=="https://api.openai.com:443/v1/chat/completions";
    if(!deck::aiRequest(config.model,history_,prompt.c_str(),job->body,openAiProfile)){delete job;status="Use 1-384 characters and a valid model";return false;}
    job->key=config.key;
    if(xQueueSend(jobs_,&job,0)!=pdTRUE){delete job;status="AI busy; try again";return false;}
    pending_=prompt.c_str();busy=true;status="Asking AI over Wi-Fi...";return true;
}
bool AiChat::poll() {
    Job* result=nullptr;if(!results_ || xQueueReceive(results_,&result,0)!=pdTRUE)return false;
    busy=false;
    if(result->reply.ok) {
        // Only completed turns are sent in a later request. A failed/uncertain
        // prompt is never silently replayed when the user asks a new question.
        while(history_.size()+2>deck::AiHistoryLimit)history_.erase(history_.begin(),history_.begin()+2);
        history_.push_back({false,pending_});history_.push_back({true,std::move(result->reply.text)});
        status=result->reply.truncated?"Reply shortened to fit; J/K read, N follow up":"Reply ready. J/K read, N follow up";
    } else status=result->reply.error.c_str();
    pending_.clear();delete result;return true;
}
bool AiChat::clear() {
    if(busy){status="Wait for the AI request before clearing";return false;}
    history_.clear();pending_.clear();status="Chat cleared from this device";return true;
}
deck::AiReply AiChat::request(const Job& job) {
    if(WiFi.status()!=WL_CONNECTED)return failure("Wi-Fi disconnected. No request sent.");
    // Certificate validity checks need the real date. Sync only following an
    // explicit send, never on startup. Do not fall back to insecure TLS.
    if(std::time(nullptr)<1704067200) {
        configTime(0,0,"time.cloudflare.com","pool.ntp.org");uint32_t at=millis();
        while(std::time(nullptr)<1704067200 && uint32_t(millis()-at)<12000)delay(100);
        if(std::time(nullptr)<1704067200)return failure("Clock sync failed; cannot verify HTTPS certificate.");
    }
    if(ESP.getFreeHeap()<90000)return failure("Not enough memory for HTTPS. Turn Bluetooth off.");
    esp_http_client_config_t config{};config.url=job.url.c_str();config.method=HTTP_METHOD_POST;
    config.timeout_ms=12000;config.disable_auto_redirect=true;config.max_authorization_retries=-1;
    config.crt_bundle_attach=esp_crt_bundle_attach;config.buffer_size=1024;config.buffer_size_tx=1024;
    auto client=esp_http_client_init(&config);if(!client)return failure("Could not allocate HTTPS connection.");
    esp_http_client_set_header(client,"Content-Type","application/json");
    esp_http_client_set_header(client,"Accept","application/json");
    std::string authorization;
    if(!job.key.empty()){authorization="Bearer "+job.key;esp_http_client_set_header(client,"Authorization",authorization.c_str());}
    const uint32_t started=millis();int status=-1;std::string body;bool tooLarge=false,complete=false;
    auto err=esp_http_client_open(client,int(job.body.size()));
    if(err==ESP_OK) {
        size_t sent=0;
        while(sent<job.body.size() && uint32_t(millis()-started)<RequestMillis) {
            int count=esp_http_client_write(client,job.body.data()+sent,int(job.body.size()-sent));
            if(count<=0)break;
            sent+=size_t(count);
        }
        if(sent==job.body.size() && uint32_t(millis()-started)<RequestMillis) {
            int64_t length=esp_http_client_fetch_headers(client);status=esp_http_client_get_status_code(client);
            if(length>int64_t(deck::AiBodyLimit))tooLarge=true;
            if(length>=0 && status>=200 && status<300 && !tooLarge) {
                char chunk[256];
                while(uint32_t(millis()-started)<RequestMillis) {
                    int count=esp_http_client_read(client,chunk,sizeof(chunk));if(count<0)break;
                    if(!count){complete=esp_http_client_is_complete_data_received(client);break;}
                    if(body.size()+size_t(count)>deck::AiBodyLimit){tooLarge=true;break;}
                    body.append(chunk,size_t(count));
                    if(esp_http_client_is_complete_data_received(client)){complete=true;break;}
                }
            } else if(status>=300)complete=true; // Never display a provider's error body: it may echo secrets.
        }
    }
    esp_http_client_close(client);esp_http_client_cleanup(client);
    volatile char* bearer=authorization.empty()?nullptr:&authorization[0];for(size_t n=0;n<authorization.size();++n)bearer[n]=0;
    if(tooLarge)return failure("AI response exceeded 8 KB. No retry sent.");
    if(!complete)return failure("Network/TLS timeout; result unknown. No retry sent.");
    return deck::aiResponse(status,body);
}
void AiChat::worker(void* self) {
    auto& chat=*static_cast<AiChat*>(self);
    for(;;) {
        Job* job=nullptr;if(xQueueReceive(chat.jobs_,&job,portMAX_DELAY)!=pdTRUE)continue;
        job->reply=request(*job);
        // Wipe the copied bearer credential before freeing the request snapshot.
        volatile char* key=job->key.empty()?nullptr:&job->key[0];for(size_t n=0;n<job->key.size();++n)key[n]=0;
        job->key.clear();job->body.clear();
        xQueueSend(chat.results_,&job,portMAX_DELAY);
    }
}
