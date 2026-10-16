#include "ai_protocol.h"
#include "ai_models.h"
#include <algorithm>
#include <cstring>
#include <utility>
namespace deck {
namespace {
bool utf8Valid(const std::string& s) {
    for(size_t n=0;n<s.size();) {
        unsigned char first=s[n++];if(first<128)continue;
        unsigned need=first>=0xc2 && first<=0xdf?1:first>=0xe0 && first<=0xef?2:first>=0xf0 && first<=0xf4?3:4;
        if(need==4 || s.size()-n<need)return false;
        unsigned code=first&((1u<<(6-need))-1u);
        for(unsigned i=0;i<need;++i){unsigned char c=s[n++];if((c&0xc0)!=0x80)return false;code=(code<<6)|(c&63);}
        if((need==2 && code<0x800) || (need==3 && code<0x10000) || code>0x10ffff || (code>=0xd800 && code<=0xdfff))return false;
    }
    return true;
}
bool printable(const std::string& s,size_t max,bool empty=true) {
    if(s.size()>max || (!empty && s.empty()))return false;
    for(unsigned char c:s)if(c<33 || c>126)return false;
    return true;
}
std::string quoted(const std::string& s) {
    const char* hex="0123456789abcdef";std::string out="\"";
    for(unsigned char c:s) {
        if(c=='"' || c=='\\'){out+='\\';out+=char(c);}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
// Only strings at these exact JSON paths are retained. Unknown fields still
// undergo bounded syntax validation; no substring matching of provider output.
class Parser {
    const std::string& data;size_t p=0,nodes=0;
    static void utf8(std::string& out,unsigned v) {
        if(v<128)out+=char(v);
        else if(v<2048){out+=char(0xc0|(v>>6));out+=char(0x80|(v&63));}
        else if(v<65536){out+=char(0xe0|(v>>12));out+=char(0x80|((v>>6)&63));out+=char(0x80|(v&63));}
        else {out+=char(0xf0|(v>>18));out+=char(0x80|((v>>12)&63));out+=char(0x80|((v>>6)&63));out+=char(0x80|(v&63));}
    }
    void ws(){while(p<data.size() && (data[p]==' ' || data[p]=='\r' || data[p]=='\n' || data[p]=='\t'))++p;}
    bool hex4(unsigned& out) {
        out=0;if(data.size()-p<4)return false;
        for(int n=0;n<4;++n){unsigned char c=data[p++];unsigned v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;if(v==16)return false;out=(out<<4)|v;}
        return true;
    }
    bool string(std::string& out) {
        if(p>=data.size() || data[p++]!='"')return false;
        while(p<data.size()) {
            unsigned char c=data[p++];if(c=='"')return true;if(c<32)return false;
            if(c!='\\'){out+=char(c);continue;}
            if(p>=data.size())return false;
            c=data[p++];
            if(c=='"' || c=='\\' || c=='/')out+=char(c);
            else if(c=='b')out+='\b';else if(c=='f')out+='\f';else if(c=='n')out+='\n';else if(c=='r')out+='\r';else if(c=='t')out+='\t';
            else if(c=='u') {
                unsigned v=0;if(!hex4(v))return false;
                if(v>=0xd800 && v<=0xdbff){if(data.size()-p<6 || data[p++]!='\\' || data[p++]!='u')return false;unsigned low=0;if(!hex4(low) || low<0xdc00 || low>0xdfff)return false;v=0x10000+((v-0xd800)<<10)+(low-0xdc00);}
                else if(v>=0xdc00 && v<=0xdfff)return false;
                if(!v)return false;
                utf8(out,v);
            } else return false;
        }
        return false;
    }
    bool value(const std::string& path,unsigned depth) {
        ws();if(p>=data.size() || depth>16 || ++nodes>1024)return false;
        char c=data[p];
        if((path=="/choices" && c!='[') || ((path=="/choices/0" || path=="/choices/0/message") && c!='{'))return false;
        if(c=='"') {
            std::string s;if(!string(s))return false;
            if(path=="/choices/0/message/content") {if(contentSeen)return false;contentSeen=true;content=std::move(s);}
            else if(path=="/choices/0/finish_reason")finish=std::move(s);
            return true;
        }
        if(c=='{' || c=='[') {
            const bool object=c=='{';++p;ws();if(p<data.size() && data[p]==(object?'}':']')){++p;return true;}
            unsigned index=0;
            do {
                std::string name;if(object){ws();if(!string(name))return false;ws();if(p>=data.size() || data[p++]!=':')return false;}
                else name=std::to_string(index++);
                if(!value(name.find('/')==std::string::npos?path+'/'+name:"/unknown",depth+1))return false;
                ws();if(p>=data.size())return false;c=data[p++];
                if(c==(object?'}':']'))return true;
                if(c!=',')return false;
            }while(true);
        }
        for(const char* literal:{"null","true","false"}) {
            size_t len=std::strlen(literal);if(data.compare(p,len,literal)==0){p+=len;return true;}
        }
        size_t start=p;if(data[p]=='-')++p;if(p>=data.size())return false;
        if(data[p]=='0')++p;else {if(data[p]<'1' || data[p]>'9')return false;while(p<data.size() && data[p]>='0' && data[p]<='9')++p;}
        if(p<data.size() && data[p]=='.'){++p;size_t first=p;while(p<data.size() && data[p]>='0' && data[p]<='9')++p;if(first==p)return false;}
        if(p<data.size() && (data[p]=='e' || data[p]=='E')){++p;if(p<data.size() && (data[p]=='+' || data[p]=='-'))++p;size_t first=p;while(p<data.size() && data[p]>='0' && data[p]<='9')++p;if(first==p)return false;}
        return p>start;
    }
public:
    std::string content,finish;bool contentSeen=false;
    explicit Parser(const std::string& s):data(s){}
    bool parse(){ws();if(p>=data.size() || data[p]!='{')return false;if(!value("",0))return false;ws();return p==data.size();}
};
}
bool validAiEndpoint(const std::string& value) {
    if(!printable(value,191,false) || value.compare(0,8,"https://")!=0 || value.find_first_of("?#@\\")!=std::string::npos)return false;
    size_t end=value.find('/',8);if(end==std::string::npos)end=value.size();
    auto authority=value.substr(8,end-8);if(authority.empty())return false;
    auto colon=authority.find(':');auto host=authority.substr(0,colon);
    if(host.empty() || host.front()=='.' || host.back()=='.' || host.front()=='-' || host.back()=='-')return false;
    for(unsigned char c:host)if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='.' || c=='-'))return false;
    if(colon!=std::string::npos){auto port=authority.substr(colon+1);if(port.empty() || port.size()>5)return false;unsigned n=0;for(char c:port){if(c<'0'||c>'9')return false;n=n*10+unsigned(c-'0');}if(!n || n>65535)return false;}
    return true;
}
bool validAiConfig(const AiConfig& v) {
    return v.magic==0x444b4101 && !v.endpoint[191] && !v.model[95] && !v.key[255] &&
        validAiEndpoint(v.endpoint) && printable(v.model,95) && printable(v.key,255);
}
uint32_t aiConfigChecksum(const AiConfig& v) {
    auto* p=reinterpret_cast<const uint8_t*>(&v);uint32_t hash=2166136261u;
    for(size_t n=0;n<offsetof(AiConfig,checksum);++n){hash^=p[n];hash*=16777619u;}return hash;
}
std::string aiCompletionUrl(const std::string& endpoint) {
    if(!validAiEndpoint(endpoint))return {};
    std::string url=endpoint;while(url.back()=='/')url.pop_back();
    constexpr const char* suffix="/chat/completions";
    if(url.size()<std::strlen(suffix) || url.compare(url.size()-std::strlen(suffix),std::strlen(suffix),suffix))url+=suffix;
    return url;
}
bool aiRequest(const std::string& model,const std::vector<AiMessage>& history,const std::string& prompt,std::string& json,bool openAiEndpoint) {
    json.clear();if(!printable(model,95,false) || prompt.empty() || prompt.size()>AiPromptLimit || history.size()>AiHistoryLimit)return false;
    if(std::all_of(prompt.begin(),prompt.end(),[](unsigned char c){return c<=32;}))return false;
    json="{\"model\":"+quoted(model)+",\"messages\":[{\"role\":\"system\",\"content\":"+quoted(AiSystemPrompt)+'}';
    for(const auto& m:history) {
        if(m.text.empty() || m.text.size()>AiReplyLimit){json.clear();return false;}
        if(!utf8Valid(m.text)){json.clear();return false;}
        json+=",{\"role\":";json+=m.assistant?"\"assistant\"":"\"user\"";json+=",\"content\":"+quoted(m.text)+'}';
    }
    if(!utf8Valid(prompt)){json.clear();return false;}
    json+=",{\"role\":\"user\",\"content\":"+quoted(prompt)+"}],\"stream\":false";
    if(openAiEndpoint && boundedOpenAiModel(model))json+=",\"max_completion_tokens\":256,\"reasoning_effort\":\"none\"";
    json+='}';
    if(json.size()>16384){json.clear();return false;}return true;
}
AiReply aiResponse(int status,const std::string& body) {
    AiReply reply;
    if(status<0){reply.error="Network/TLS error; result unknown. No retry sent.";return reply;}
    if(status<200 || status>=300) {
        if(status==401 || status==403)reply.error="API rejected credentials or model access ("+std::to_string(status)+").";
        else if(status==429)reply.error="API limit or credit balance reached (429).";
        else if(status>=300 && status<400)reply.error="Redirect refused. Set the final HTTPS API address.";
        else reply.error="API returned HTTP "+std::to_string(status)+". Check endpoint and model.";
        return reply;
    }
    if(body.size()>AiBodyLimit){reply.error="API response exceeded 8 KB. No retry sent.";return reply;}
    Parser parser(body);
    if(!utf8Valid(body) || !parser.parse() || !parser.contentSeen || parser.content.empty()){reply.error="API returned no supported text response.";return reply;}
    reply.ok=true;reply.text=std::move(parser.content);reply.truncated=parser.finish=="length";
    // The response has already passed UTF-8 validation. Count whole Unicode
    // code points, including spaces/newlines, and retain both independent caps.
    size_t end=0,characters=0;
    while(end<reply.text.size() && characters<AiReplyCharacters) {
        const auto first=static_cast<unsigned char>(reply.text[end]);
        const size_t bytes=first<0x80?1:first<0xe0?2:first<0xf0?3:4;
        if(end+bytes>AiReplyLimit)break;
        end+=bytes;++characters;
    }
    if(end<reply.text.size()){reply.text.resize(end);reply.truncated=true;}
    return reply;
}
std::vector<std::string> aiDisplayLines(const std::vector<AiMessage>& history,size_t columns) {
    std::vector<std::string> lines;if(!columns)return lines;
    for(const auto& m:history) {
        lines.push_back(m.assistant?"AI:":"You:");std::string line;
        for(unsigned char c:m.text) {
            if(c=='\r')continue;
            if(c=='\n'){lines.push_back(line);line.clear();continue;}
            if(c>=0x80 && c<0xc0)continue;
            if(c>=0xc0)c='?';else if(c<32)c=' ';
            line+=char(c);if(line.size()==columns){lines.push_back(line);line.clear();}
        }
        if(!line.empty())lines.push_back(line);
        lines.emplace_back();
    }
    while(!lines.empty() && lines.back().empty())lines.pop_back();
    return lines;
}
size_t aiLastPageOffset(const std::vector<std::string>& lines,size_t pageSize) {
    if(!pageSize)return 0;
    size_t end=lines.size();while(end && lines[end-1].empty())--end;
    return end?((end-1)/pageSize)*pageSize:0;
}
}
