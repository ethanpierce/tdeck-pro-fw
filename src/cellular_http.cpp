#include "cellular_http.h"
#include "cellular_socket.h"
#include <cellular_http_protocol.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>
#include <sys/time.h>
#include <ctime>
#include <algorithm>
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace {
constexpr uint32_t RequestMillis=120000;
deck::AiReply failure(const char* message){deck::AiReply result;result.error=message;return result;}
void wipe(std::string& value){volatile char* data=value.empty()?nullptr:&value[0];for(size_t n=0;n<value.size();++n)data[n]=0;value.clear();}
struct Tls {
    mbedtls_ssl_context ssl;mbedtls_ssl_config config;mbedtls_entropy_context entropy;mbedtls_ctr_drbg_context random;
    uint32_t started=millis();bool connected=false;
    Tls(){mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&random);}
    ~Tls(){mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_ctr_drbg_free(&random);mbedtls_entropy_free(&entropy);if(connected)cellTcpClose();}
    bool expired()const{return uint32_t(millis()-started)>=RequestMillis;}
    static int send(void* opaque,const unsigned char* bytes,size_t length) {
        auto& self=*static_cast<Tls*>(opaque);if(self.expired()){return MBEDTLS_ERR_NET_SEND_FAILED;}
        const int sent=cellTcpWrite(bytes,length);return sent>0?sent:MBEDTLS_ERR_NET_SEND_FAILED;
    }
    static int receive(void* opaque,unsigned char* bytes,size_t length) {
        auto& self=*static_cast<Tls*>(opaque);if(self.expired()){return MBEDTLS_ERR_NET_RECV_FAILED;}
        const int read=cellTcpRead(bytes,length);
        if(read>0){return read;}
        if(read==0){return MBEDTLS_ERR_SSL_WANT_READ;}
        return read==-2?0:MBEDTLS_ERR_NET_RECV_FAILED;
    }
};
}
deck::AiReply cellularAiRequest(const std::string& url,const std::string& key,const std::string& body) {
    deck::HttpsAddress address;if(!deck::cellularHttpsAddress(url,address)){return failure("Invalid HTTPS endpoint for LTE.");}
    if(!cellTcpReady()){return failure("LTE data unavailable. Check SIM, signal and APN.");}
    if(std::time(nullptr)<1704067200) {
        const int64_t timestamp=cellNetworkTime();
        if(timestamp<1704067200){return failure("LTE clock unavailable. Connect Wi-Fi once to set time.");}
        timeval time{time_t(timestamp),0};if(settimeofday(&time,nullptr)!=0){return failure("Could not set the HTTPS verification clock.");}
    }
    if(ESP.getFreeHeap()<100000){return failure("Not enough memory for LTE HTTPS. Turn Bluetooth off.");}
    Tls tls;
    if(!cellTcpOpen(address.host,address.port)){return failure("LTE TCP failed. Check data/APN or restart the modem.");}
    tls.connected=true;
    constexpr unsigned char seed[]="Handheld OS LTE TLS";
    if(mbedtls_ctr_drbg_seed(&tls.random,mbedtls_entropy_func,&tls.entropy,seed,sizeof(seed)-1)!=0 ||
       mbedtls_ssl_config_defaults(&tls.config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)
        return failure("Could not initialize LTE TLS. No API request sent.");
    mbedtls_ssl_conf_authmode(&tls.config,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_version(&tls.config,MBEDTLS_SSL_MAJOR_VERSION_3,MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_rng(&tls.config,mbedtls_ctr_drbg_random,&tls.random);
    if(esp_crt_bundle_attach(&tls.config)!=ESP_OK || mbedtls_ssl_setup(&tls.ssl,&tls.config)!=0 ||
       mbedtls_ssl_set_hostname(&tls.ssl,address.host.c_str())!=0)
        return failure("Could not attach trusted LTE TLS certificates. No API request sent.");
    mbedtls_ssl_set_bio(&tls.ssl,&tls,Tls::send,Tls::receive,nullptr);
    int result=0;
    while((result=mbedtls_ssl_handshake(&tls.ssl))!=0) {
        if(tls.expired() || (result!=MBEDTLS_ERR_SSL_WANT_READ && result!=MBEDTLS_ERR_SSL_WANT_WRITE))
            return failure("LTE TLS verification/handshake failed. No API request sent.");
        delay(10);
    }
    if(mbedtls_ssl_get_verify_result(&tls.ssl)!=0){return failure("LTE TLS certificate rejected. No API request sent.");}

    std::string request="POST "+address.path+" HTTP/1.1\r\nHost: "+address.host;
    if(address.port!=443)request+=":"+std::to_string(address.port);
    request+="\r\nContent-Type: application/json\r\nAccept: application/json\r\nConnection: close\r\n";
    if(!key.empty())request+="Authorization: Bearer "+key+"\r\n";
    request+="Content-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    size_t sent=0;
    while(sent<request.size() && !tls.expired()) {
        result=mbedtls_ssl_write(&tls.ssl,reinterpret_cast<const unsigned char*>(request.data()+sent),std::min(size_t(1024),request.size()-sent));
        if(result>0)sent+=size_t(result);
        else if(result==MBEDTLS_ERR_SSL_WANT_READ || result==MBEDTLS_ERR_SSL_WANT_WRITE)delay(10);
        else break;
    }
    const bool written=sent==request.size();wipe(request);
    if(!written){return failure("LTE upload interrupted; result unknown. No retry sent.");}
    deck::CellHttpResponse response;unsigned char buffer[512];
    while(!tls.expired() && !response.done()) {
        result=mbedtls_ssl_read(&tls.ssl,buffer,sizeof(buffer));
        if(result>0){if(!response.feed(reinterpret_cast<const char*>(buffer),size_t(result)))break;}
        else if(result==MBEDTLS_ERR_SSL_WANT_READ || result==MBEDTLS_ERR_SSL_WANT_WRITE)delay(20);
        else {response.eof(result==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY);break;}
    }
    if(!response.done()){return failure("LTE response incomplete/over limit; result unknown. No retry sent.");}
    return deck::aiResponse(response.status(),response.body());
}
