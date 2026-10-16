#include "connectivity.h"
#include <at_engine.h>
#include <algorithm>
#include <array>
#include <cctype>
namespace deck {
CellularIndicator cellularIndicator(bool linked,bool simReady,int registration,int dbm,
    const std::string& operatorInfo,const std::string& systemInfo) {
    if(!linked)return {"...",0};
    if(!simReady)return {"SIM",0};
    if(registration==8 || registration==11)return {"SOS",0};
    if(registration==2 || registration<0)return {"...",0};
    const bool registered=registration==1 || registration==5 || registration==6 ||
        registration==7 || registration==9 || registration==10;
    if(!registered)return {"NO",0};
    const char* label="NET";
    auto fields=csv(operatorInfo);
    if(fields.size()>=4) {
        const auto& act=fields[3];
        if(act=="7")label="LTE";
        else if(act=="0" || act=="1" || act=="3")label="2G";
        else if(act=="2" || act=="4" || act=="5" || act=="6")label="3G";
    }
    // COPS is polled regularly; use CPSI only until the access technology is known.
    if(std::string(label)=="NET") {
        if(systemInfo.rfind("LTE,",0)==0)label="LTE";
        else if(systemInfo.rfind("GSM,",0)==0 || systemInfo.rfind("GPRS,",0)==0 || systemInfo.rfind("EDGE,",0)==0)label="2G";
    }
    if(registration==6 || registration==7)label="SMS";
    uint8_t bars=0;
    if(dbm>=-113 && dbm<=-51)bars=dbm>=-75?4:dbm>=-85?3:dbm>=-95?2:1;
    return {label,bars};
}
std::vector<CarrierNetwork> parseCarrierScan(const std::string& line) {
    std::vector<CarrierNetwork> out;
    if(line.rfind("+COPS:",0)!=0 || line.size()>4096) return out;
    bool quoted=false; size_t start=std::string::npos;
    for(size_t i=6;i<line.size();++i) {
        if(line[i]=='"') quoted=!quoted;
        if(quoted) continue;
        if(line[i]=='(') start=i+1;
        else if(line[i]==')' && start!=std::string::npos) {
            auto fields=csv(line.substr(start,i-start));start=std::string::npos;
            if(fields.size()!=5 || fields[0].size()!=1 || fields[0][0]<'0' || fields[0][0]>'3' ||
               fields[4].empty() || fields[4].size()>2 || fields[3].size()<5 || fields[3].size()>6) continue;
            auto digits=[](const std::string& s){return std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});};
            if(!digits(fields[3]) || !digits(fields[4]) || fields[1].size()>80) continue;
            int tech=0;for(char c:fields[4])tech=tech*10+c-'0';
            if(out.size()<16) out.push_back({fields[0][0]-'0',tech,fields[1].empty()?fields[2]:fields[1],fields[3]});
        }
    }
    return out;
}
bool validWifiCredentials(const std::string& ssid,const std::string& password) {
    if(ssid.empty() || ssid.size()>32 || ssid.find('\0')!=std::string::npos || password.find('\0')!=std::string::npos) return false;
    if(password.empty()) return true;
    if(password.size()==64) return std::all_of(password.begin(),password.end(),[](unsigned char c){return std::isxdigit(c);});
    return password.size()>=8 && password.size()<=63 && std::all_of(password.begin(),password.end(),[](unsigned char c){return c>=32 && c<=126;});
}
bool validBleAddress(const std::string& address) {
    if(address.size()!=17) return false;
    for(size_t i=0;i<17;++i) if(i%3==2?address[i]!=':':!std::isxdigit(static_cast<unsigned char>(address[i]))) return false;
    return true;
}
bool keyboardLayout(const uint8_t* map,size_t size,uint8_t reportId,HidKeyboardLayout& out) {
    if(!map || !size || size>1024) return false;
    struct Global {uint32_t page=0,bits=0,count=0,id=0;} g;
    std::vector<Global> stack;
    std::array<uint32_t,256> offsets{};
    uint32_t usageMin=0,usageMax=0;bool haveMin=false,haveMax=false,mod=false,keys=false;
    HidKeyboardLayout result{};
    for(size_t at=0;at<size;) {
        uint8_t head=map[at++];if(head==0xfe)return false;
        size_t n=(head&3)==3?4:head&3;if(at+n>size)return false;
        uint32_t value=0;for(size_t j=0;j<n;++j)value|=uint32_t(map[at++])<<(8*j);
        unsigned type=(head>>2)&3,tag=head>>4;
        if(type==1) {
            if(tag==0)g.page=value;else if(tag==7)g.bits=value;else if(tag==8){if(!value || value>255)return false;g.id=value;}
            else if(tag==9)g.count=value;
            else if(tag==10){if(stack.size()>=4)return false;stack.push_back(g);}
            else if(tag==11){if(stack.empty())return false;g=stack.back();stack.pop_back();}
        } else if(type==2) {
            if(tag==1){usageMin=value;haveMin=true;}else if(tag==2){usageMax=value;haveMax=true;}
        } else if(type==0) {
            if(tag==8) {
                if(g.bits>256 || g.count>256 || offsets[g.id]+g.bits*g.count>256)return false;
                auto bit=offsets[g.id];
                if(g.id==reportId && g.page==7 && !(value&1)) {
                    if((value&2) && g.bits==1 && g.count==8 && bit%8==0 && haveMin && haveMax && usageMin==224 && usageMax==231) {
                        if(mod)return false;
                        result.modifier=bit/8;mod=true;
                    } else if(!(value&2) && g.bits==8 && g.count>=1 && g.count<=16 && bit%8==0) {
                        if(keys)return false;
                        result.keys=bit/8;result.count=g.count;keys=true;
                    } else if((value&2) && g.bits==1 && g.count>=1 && g.count<=232 && haveMin && haveMax &&
                              usageMin<=231 && usageMax<=231 && usageMax>=usageMin && usageMax-usageMin+1==g.count) {
                        if(keys)return false;
                        result.keys=bit;result.count=g.count;result.bitmap=1;result.firstUsage=usageMin;keys=true;
                    } else return false;
                }
                offsets[g.id]+=g.bits*g.count;
            }
            haveMin=haveMax=false;
        }
    }
    if(!mod || !keys || offsets[reportId]%8)return false;
    result.bytes=offsets[reportId]/8;if(result.bytes>32 || !result.bytes)return false;
    out=result;return true;
}
std::string HidKeyboardDecoder::feed(const uint8_t* report,size_t size) {
    std::string out;
    if(!report || size!=layout_.bytes || layout_.modifier>=size)return out;
    std::vector<uint8_t> now;
    if(layout_.bitmap) {
        if(size_t(layout_.keys)+layout_.count>size*8)return out;
        for(unsigned i=0;i<layout_.count;++i)if(report[(layout_.keys+i)/8] & (1u<<((layout_.keys+i)%8)))now.push_back(layout_.firstUsage+i);
    } else {
        if(size_t(layout_.keys)+layout_.count>size)return out;
        now.assign(report+layout_.keys,report+layout_.keys+layout_.count);
    }
    for(uint8_t code:now)if(code>=1 && code<=3)return out; // Rollover is not a release.
    bool shift=report[layout_.modifier]&0x22;
    for(uint8_t code:now) {
        if(!code || std::find(previous_.begin(),previous_.end(),code)!=previous_.end())continue;
        if(code==57){caps_=!caps_;continue;}
        if(report[layout_.modifier]&0xdd)continue; // Do not turn Ctrl/Alt/GUI shortcuts into text.
        char c=0;
        if(code>=4 && code<=29){c='a'+code-4;if(shift!=caps_)c-=32;}
        else if(code>=30 && code<=39)c=(shift?"!@#$%^&*()":"1234567890")[code-30];
        else if(code==40)c='\r';else if(code==41)c=27;else if(code==42)c='\b';else if(code==44)c=' ';
        else if(code>=45 && code<=56 && code!=50)c=(shift?"_+{}|~:\"~<>?":"-=[]\\~;'`,./")[code-45];
        if(c)out+=c;
    }
    previous_=std::move(now);return out;
}
}
