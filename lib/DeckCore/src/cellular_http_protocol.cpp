#include "cellular_http_protocol.h"
#include "ai_protocol.h"
#include <algorithm>
#include <cstdlib>

namespace deck {
namespace {
int hexDigit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
std::string lower(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+=32;return value;}
bool decimal(const std::string& s,size_t limit,size_t& n){if(s.empty()){return false;}n=0;for(char c:s){if(c<'0'||c>'9'||n>(limit-size_t(c-'0'))/10){return false;}n=n*10+size_t(c-'0');}return n<=limit;}
}
bool cellHex(const std::string& hex,size_t expected,std::string& bytes) {
    bytes.clear();if(expected>256 || hex.size()!=expected*2){return false;}
    for(size_t n=0;n<hex.size();n+=2){int a=hexDigit(hex[n]),b=hexDigit(hex[n+1]);if(a<0||b<0){bytes.clear();return false;}bytes+=char((a<<4)|b);}return true;
}
int64_t cellClockUtc(const std::string& response) {
    const auto begin=response.find('"');if(begin==std::string::npos){return 0;}
    auto s=response.substr(begin+1);if(s.size()!=21 || s.back()!='"'){return 0;}
    if(s[2]!='/'||s[5]!='/'||s[8]!=','||s[11]!=':'||s[14]!=':'||(s[17]!='+'&&s[17]!='-')){return 0;}
    auto pair=[&](size_t p){return s[p]>='0'&&s[p]<='9'&&s[p+1]>='0'&&s[p+1]<='9'?(s[p]-'0')*10+s[p+1]-'0':-1;};
    int y=pair(0)+2000,m=pair(3),d=pair(6),h=pair(9),min=pair(12),sec=pair(15),zone=pair(18);
    if(y<2024||y>2099||m<1||m>12||h<0||h>23||min<0||min>59||sec<0||sec>59||zone<0||zone>56){return 0;}
    const int months[]={31,28+(y%4==0),31,30,31,30,31,31,30,31,30,31};if(d<1||d>months[m-1]){return 0;}
    int64_t days=0;for(int year=1970;year<y;++year)days+=365+(year%4==0 && (year%100!=0||year%400==0));
    for(int month=1;month<m;++month){days+=months[month-1];}
    days+=d-1;
    return days*86400+h*3600+min*60+sec-(s[17]=='+'?1:-1)*zone*900;
}
bool cellularHttpsAddress(const std::string& url,HttpsAddress& out) {
    if(url.size()>224 || url.compare(0,8,"https://")!=0 || url.find_first_of("?#@\\")!=std::string::npos){return false;}
    for(unsigned char c:url)if(c<33 || c>126){return false;}
    auto end=url.find('/',8);if(end==std::string::npos)end=url.size();auto authority=url.substr(8,end-8);
    if(!validAiEndpoint("https://"+authority)){return false;}
    auto colon=authority.find(':');out.host=authority.substr(0,colon);out.port=443;
    if(colon!=std::string::npos){size_t port=0;if(!decimal(authority.substr(colon+1),65535,port)||!port){return false;}out.port=uint16_t(port);}
    out.path=end==url.size()?"/":url.substr(end);
    return true;
}
bool CellHttpResponse::line() {
    if(line_.size()<2 || line_.compare(line_.size()-2,2,"\r\n")){return error();}
    std::string value=line_.substr(0,line_.size()-2);line_.clear();
    if(state_==Headers) {
        if(!status_) {
            if(value.size()<12 || (value.compare(0,9,"HTTP/1.1 ") && value.compare(0,9,"HTTP/1.0 "))){return error();}
            size_t status=0;if(!decimal(value.substr(9,3),599,status)||status<200){return error();}
            if(value.size()>12 && value[12]!=' '){return error();}status_=int(status);return true;
        }
        if(value.empty()) {
            if(haveLength_ && chunked_){return error();}
            if(status_>=300 || status_==204){state_=Done;return true;}
            state_=chunked_?ChunkSize:haveLength_?(remaining_?LengthBody:Done):CloseBody;return true;
        }
        auto colon=value.find(':');if(colon==std::string::npos || !colon){return error();}
        auto name=lower(value.substr(0,colon));
        for(char c:name)if(!((c>='a'&&c<='z')||c=='-'||(c>='0'&&c<='9'))){return error();}
        auto content=value.substr(colon+1);auto first=content.find_first_not_of(" \t");content=first==std::string::npos?"":content.substr(first,content.find_last_not_of(" \t")-first+1);
        for(unsigned char c:content)if((c<32&&c!='\t')||c==127){return error();}
        if(name=="content-length") {
            if(haveLength_ || !decimal(content,AiBodyLimit,remaining_)){return error();}haveLength_=true;
        } else if(name=="transfer-encoding") {
            if(haveTransfer_ || lower(content)!="chunked"){return error();}haveTransfer_=chunked_=true;
        } else if(name=="content-encoding" && !content.empty() && lower(content)!="identity"){return error();}
        return true;
    }
    if(state_==ChunkSize) {
        auto semicolon=value.find(';');auto size=value.substr(0,semicolon);if(size.empty()||size.size()>8){return error();}
        remaining_=0;for(char c:size){int digit=hexDigit(c);if(digit<0||remaining_>(AiBodyLimit-size_t(digit))/16){return error();}remaining_=remaining_*16+size_t(digit);}
        if(remaining_>AiBodyLimit-body_.size()){return error();}
        state_=remaining_?ChunkBody:Trailers;return true;
    }
    if(state_==Trailers) {if(value.empty())state_=Done;else if(value.find(':')==std::string::npos || value.front()==' ' || value.front()=='\t'){return error();}return true;}
    return error();
}
bool CellHttpResponse::feed(const char* data,size_t size) {
    if(state_==Error){return false;}
    for(size_t n=0;n<size && state_!=Done;++n) {
        char c=data[n];
        if(state_==LengthBody || state_==ChunkBody || state_==CloseBody) {
            if(body_.size()>=AiBodyLimit){return error();}body_+=c;
            if(state_!=CloseBody && !--remaining_){if(state_==LengthBody)state_=Done;else{state_=ChunkEnd;chunkEnd_=0;}}
        } else if(state_==ChunkEnd) {
            if(c!=(chunkEnd_?'\n':'\r')){return error();}if(++chunkEnd_==2)state_=ChunkSize;
        } else {
            if(++headerBytes_>4096 || line_.size()>=1024){return error();}line_+=c;
            if(c=='\n' && !line()){return false;}
        }
    }
    return true;
}
bool CellHttpResponse::eof(bool authenticatedClose) {
    if(state_==CloseBody && authenticatedClose)state_=Done;
    return state_==Done;
}
}
