#include <cellular_http_protocol.h>
#include <at_engine.h>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

namespace {
deck::CellHttpResponse parse(const std::string& bytes,size_t chunk=1) {
    deck::CellHttpResponse result;
    for(size_t n=0;n<bytes.size();n+=chunk)if(!result.feed(bytes.data()+n,std::min(chunk,bytes.size()-n)))break;
    return result;
}
}
int main() {
    using namespace deck;
    std::string decoded;
    assert(cellHex("000d0a1aff4f4b",7,decoded) && decoded==std::string("\0\r\n\x1a\xffOK",7));
    assert(!cellHex("00zz",2,decoded) && decoded.empty());
    assert(!cellHex("00",2,decoded));assert(!cellHex(std::string(514,'0'),257,decoded));
    assert(cellClockUtc("+CCLK: \"24/01/01,00:00:00+00\"")==1704067200);
    assert(cellClockUtc("+CCLK: \"24/01/01,00:00:00+04\"")==1704063600);
    assert(cellClockUtc("+CCLK: \"24/01/01,00:00:00-04\"")==1704070800);
    for(const auto* clock:{"+CCLK: \"23/01/01,00:00:00+00\"","+CCLK: \"25/02/29,00:00:00+00\"","+CCLK: \"26/13/01,00:00:00+00\"","+CCLK: \"26/01/01,24:00:00+00\"","+CCLK: \"26/01/01,00:00:00+99\"","+CCLK: \"26/01/01\""})assert(!cellClockUtc(clock));
    HttpsAddress address;
    assert(cellularHttpsAddress("https://api.openai.com/v1/chat/completions",address) && address.host=="api.openai.com" && address.path=="/v1/chat/completions" && address.port==443);
    assert(cellularHttpsAddress("https://host.example:8443/api",address) && address.port==8443);
    assert(!cellularHttpsAddress("https://host.example\";ATD123;/x",address));
    assert(!cellularHttpsAddress("https://host.example/x\r\nAuthorization: bad",address));
    auto reply=parse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
    assert(reply.done() && reply.status()==200 && reply.body()=="hello");
    reply=parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhe\r\n3;test=yes\r\nllo\r\n0\r\nX-Trace: a\r\n\r\n",7);
    assert(reply.done() && reply.body()=="hello");
    reply=parse("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nhello");
    assert(!reply.done() && !reply.eof(false));assert(reply.eof(true) && reply.body()=="hello");
    reply=parse("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello");
    assert(!reply.eof(true));
    reply=parse("HTTP/1.1 401 Unauthorized\r\nContent-Length: 35\r\n\r\nsecret-echo-must-never-be-retained");
    assert(reply.done() && reply.status()==401 && reply.body().empty());
    reply=parse("HTTP/1.1 302 Found\r\nLocation: https://other.example/\r\n\r\n");
    assert(reply.done() && reply.status()==302 && reply.body().empty());
    for(const auto* response:{"HTTP/1.1 200 OK\r\nContent-Length: 8193\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n","HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n","HTTP/1.1 200 OK\n\n","HTTP/1.1 200 OK\r\n Bad: fold\r\n\r\n","HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2001\r\n","HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naX"})assert(parse(response).failed());
    reply=parse("HTTP/1.1 200 OK\r\nX-Long: "+std::string(5000,'x')+"\r\n\r\n");assert(reply.failed());
    reply=parse("HTTP/1.1 200 OK\r\nContent-Length: 8192\r\n\r\n"+std::string(8192,'x'),256);assert(reply.done() && reply.body().size()==8192);
    // Fixed-size TLS ciphertext must not acquire the SMS Ctrl-Z terminator.
    std::vector<std::string> writes,urcs;bool completed=false;
    AtEngine engine([&](const std::string& s){writes.push_back(s);},[&](const std::string& s){urcs.push_back(s);});
    AtCommand command{"AT+CIPSEND=9,4",10000,std::string("\0\r\n\x1a",4),false,[&](const AtResult& r){completed=r.ok;}};
    command.appendPayloadCtrlZ=false;assert(engine.enqueue(std::move(command)));engine.tick(1);engine.feed('>');
    assert(writes.size()==2 && writes[1]==std::string("\0\r\n\x1a",4));
    for(char c:std::string("\r\nOK\r\n+CIPSEND: 9,4,4\r\n"))engine.feed(c);
    assert(completed && urcs==std::vector<std::string>({"+CIPSEND: 9,4,4"}));
    std::mt19937 random(0x4c544548);
    for(unsigned n=0;n<4000;++n){std::string bytes;for(unsigned i=0,count=random()%2048;i<count;++i)bytes+=char(random()%256);auto fuzzed=parse(bytes,17);(void)fuzzed.eof(false);}
    std::cout<<"PASS cellular HTTP bounds/chunking/truncation, HEX framing, UTC clock, fixed binary AT payload and 4000 malformed replies\n";
}
