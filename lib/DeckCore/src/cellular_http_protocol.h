#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace deck {
bool cellHex(const std::string& hex,size_t expected,std::string& bytes);
int64_t cellClockUtc(const std::string& response);
struct HttpsAddress {std::string host,path;uint16_t port=443;};
bool cellularHttpsAddress(const std::string& url,HttpsAddress& address);
class CellHttpResponse {
public:
    bool feed(const char* data,size_t size);
    bool eof(bool authenticatedClose);
    bool done()const{return state_==Done;}
    bool failed()const{return state_==Error;}
    int status()const{return status_;}
    const std::string& body()const{return body_;}
private:
    enum State {Headers,LengthBody,CloseBody,ChunkSize,ChunkBody,ChunkEnd,Trailers,Done,Error};
    State state_=Headers;
    std::string line_,body_;
    size_t headerBytes_=0,remaining_=0,chunkEnd_=0;
    int status_=0;
    bool haveLength_=false,chunked_=false,haveTransfer_=false;
    bool line();
    bool error(){state_=Error;return false;}
};
}
