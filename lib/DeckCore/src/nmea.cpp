#include "nmea.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
namespace deck {
namespace {
int hex(char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
bool number(std::string_view s,double& result,bool sign=false){
    if(s.empty()||s.size()>16)return false;
    bool negative=sign&&s.front()=='-';if(negative)s.remove_prefix(1);
    if(s.empty())return false;
    double value=0,factor=1;bool dot=false,digit=false;
    for(char c:s){
        if(c=='.'&&!dot){dot=true;continue;}
        if(c<'0'||c>'9')return false;
        digit=true;
        if(dot){factor*=0.1;value+=(c-'0')*factor;}else value=value*10+c-'0';
        if(value>100000000)return false;
    }
    if(!digit)return false;
    result=negative?-value:value;return std::isfinite(result);
}
bool integer(std::string_view s,unsigned maximum,unsigned& value){
    value=0;if(s.empty()||s.size()>3)return false;
    for(char c:s){if(c<'0'||c>'9')return false;value=value*10+unsigned(c-'0');if(value>maximum)return false;}
    return true;
}
bool coordinate(std::string_view text,std::string_view hemisphere,bool latitude,double& result){
    double raw=0;if(!number(text,raw)||hemisphere.size()!=1)return false;
    const char positive=latitude?'N':'E',negative=latitude?'S':'W';
    if(hemisphere[0]!=positive&&hemisphere[0]!=negative)return false;
    const unsigned degrees=unsigned(raw/100);const double minutes=raw-degrees*100;
    const unsigned maximum=latitude?90:180;
    if(degrees>maximum||minutes>=60||(degrees==maximum&&minutes>0))return false;
    result=(degrees+minutes/60)*(hemisphere[0]==negative?-1:1);return true;
}
bool timeOfDay(std::string_view s,unsigned& hour,unsigned& minute,unsigned& second){
    if(s.size()<6)return false;
    if(!integer(s.substr(0,2),23,hour)||!integer(s.substr(2,2),59,minute)||!integer(s.substr(4,2),59,second))return false;
    if(s.size()>6){if(s[6]!='.'||s.size()==7)return false;for(size_t n=7;n<s.size();++n)if(s[n]<'0'||s[n]>'9')return false;}
    return true;
}
bool utc(std::string_view date,std::string_view time,char* out){
    unsigned day=0,month=0,year=0,h=0,m=0,s=0;
    if(date.size()!=6||!integer(date.substr(0,2),31,day)||!integer(date.substr(2,2),12,month)||!integer(date.substr(4,2),99,year)||!timeOfDay(time,h,m,s)||!day||!month)return false;
    year+=year>=80?1900:2000;
    const unsigned days[]={31,28u+unsigned(year%4==0),31,30,31,30,31,31,30,31,30,31};
    if(day>days[month-1])return false;
    std::snprintf(out,21,"%04u-%02u-%02uT%02u:%02u:%02uZ",year,month,day,h,m,s);return true;
}
}
void NmeaParser::reset(){*this=NmeaParser{};}
bool NmeaParser::feed(char byte,uint32_t now){
    if(byte=='$'){used_=0;collecting_=true;cr_=false;return false;}
    if(!collecting_)return false;
    if(byte=='\n'){collecting_=false;return parse(now);}
    if(byte=='\r'){if(cr_)collecting_=false;cr_=true;return false;}
    if(cr_||byte<32||byte>126||used_>=line_.size()-1){collecting_=false;return false;}
    line_[used_++]=byte;return false;
}
bool NmeaParser::parse(uint32_t now){
    if(used_<9||line_[used_-3]!='*')return false;
    int a=hex(line_[used_-2]),b=hex(line_[used_-1]);if(a<0||b<0)return false;
    uint8_t checksum=0;for(size_t n=0;n<used_-3;++n)checksum^=uint8_t(line_[n]);
    if(checksum!=uint8_t((a<<4)|b))return false;
    std::array<std::string_view,24> fields{};size_t count=0,start=0;
    for(size_t n=0;n<=used_-3;++n)if(n==used_-3||line_[n]==','){
        if(count==fields.size())return false;
        fields[count++]={line_.data()+start,n-start};start=n+1;
    }
    if(!count||fields[0].size()!=5||fields[0][0]<'A'||fields[0][0]>'Z'||fields[0][1]<'A'||fields[0][1]>'Z')return false;
    const auto type=fields[0].substr(2);double lat=0,lon=0;
    if(type=="GGA"){
        unsigned quality=0,satellites=0,h=0,m=0,s=0;
        if(count<10||!timeOfDay(fields[1],h,m,s)||!integer(fields[6],8,quality)||!integer(fields[7],99,satellites))return false;
        double hdop=-1,altitude=0;
        if(!fields[8].empty()&&(!number(fields[8],hdop)||hdop>100))return false;
        if(!fields[9].empty()&&(!number(fields[9],altitude,true)||altitude< -1000||altitude>100000))return false;
        if(!fields[9].empty()&&(count<11||fields[10]!="M"))return false;
        const bool realFix=quality>=1&&quality<=5;
        if(realFix&&(!coordinate(fields[2],fields[3],true,lat)||!coordinate(fields[4],fields[5],false,lon)))return false;
        value_.quality=uint8_t(quality);value_.satellites=uint8_t(satellites);value_.hdop=float(hdop);value_.altitudeMeters=float(altitude);
        detailsSeen_=true;detailsAt_=now;
        valid_=realFix;if(valid_){value_.latitude=lat;value_.longitude=lon;fixAt_=now;position_=true;}
    }else if(type=="RMC"){
        if(count<10||(fields[2]!="A"&&fields[2]!="V"))return false;
        char timestamp[21]{};if(!utc(fields[9],fields[1],timestamp))return false;
        const bool realMode=count<=12||fields[12].empty()||fields[12]=="A"||fields[12]=="D"||fields[12]=="F"||fields[12]=="R"||fields[12]=="P";
        bool active=fields[2]=="A"&&realMode;
        if(active&&(!coordinate(fields[3],fields[4],true,lat)||!coordinate(fields[5],fields[6],false,lon)))return false;
        std::memcpy(value_.utc,timestamp,sizeof(timestamp));utcSeen_=true;utcAt_=now;valid_=active;
        if(valid_){value_.latitude=lat;value_.longitude=lon;fixAt_=now;position_=true;}
    }else if(type=="GST"&&count>=8){
        double latitudeError=0,longitudeError=0;
        if(number(fields[6],latitudeError)&&number(fields[7],longitudeError)&&latitudeError<10000&&longitudeError<10000){value_.horizontalAccuracyMeters=float(std::hypot(latitudeError,longitudeError));accuracySeen_=true;accuracyAt_=now;}
    }
    seen_=true;sentenceAt_=now;return true;
}
GpsSnapshot NmeaParser::snapshot(uint32_t now)const{
    auto result=value_;result.fixAgeMs=position_?uint32_t(now-fixAt_):UINT32_MAX;
    result.sentenceAgeMs=seen_?uint32_t(now-sentenceAt_):UINT32_MAX;
    if(!accuracySeen_||uint32_t(now-accuracyAt_)>30000)result.horizontalAccuracyMeters=-1;
    if(!utcSeen_||uint32_t(now-utcAt_)>30000)result.utc[0]=0;
    if(!detailsSeen_||uint32_t(now-detailsAt_)>30000){result.hdop=-1;result.quality=result.satellites=0;}
    result.fix=position_&&valid_&&result.fixAgeMs<=30000;return result;
}
bool gpsMapsLink(const GpsSnapshot& value,std::string& out){
    out.clear();
    if(!value.enabled||value.suspended||!value.fix||value.fixAgeMs>30000||!std::isfinite(value.latitude)||!std::isfinite(value.longitude)||std::abs(value.latitude)>90||std::abs(value.longitude)>180)return false;
    char text[100];const int n=std::snprintf(text,sizeof(text),"https://www.google.com/maps/search/?api=1&query=%.6f%%2C%.6f",value.latitude,value.longitude);
    if(n<0||size_t(n)>=sizeof(text))return false;
    out.assign(text,size_t(n));return true;
}
}
