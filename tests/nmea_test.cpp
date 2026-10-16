#include <nmea.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
namespace {
std::string frame(const std::string& body){
    unsigned checksum=0;for(unsigned char c:body)checksum^=c;
    const char hex[]="0123456789ABCDEF";
    return "$"+body+"*"+hex[checksum>>4]+hex[checksum&15]+"\r\n";
}
bool feed(deck::NmeaParser& parser,const std::string& bytes,uint32_t at){bool result=false;for(char c:bytes)result=parser.feed(c,at)||result;return result;}
}
int main(){
    deck::NmeaParser parser;
    assert(feed(parser,frame("GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),1000));
    auto value=parser.snapshot(1200);
    assert(value.fix && value.fixAgeMs==200 && value.satellites==8 && value.quality==1);
    assert(std::abs(value.latitude-48.1173)<0.000001 && std::abs(value.longitude-11.5166666667)<0.000001);
    assert(std::abs(value.hdop-0.9)<0.001 && std::abs(value.altitudeMeters-545.4)<0.01);
    assert(feed(parser,frame("GNRMC,123520.00,A,4807.038,N,01131.000,E,0.0,0.0,161026,,,A"),2000));
    value=parser.snapshot(2001);assert(std::string(value.utc)=="2026-10-16T12:35:20Z");
    std::string link;assert(!deck::gpsMapsLink(value,link));value.enabled=true;
    assert(deck::gpsMapsLink(value,link) && link=="https://www.google.com/maps/search/?api=1&query=48.117300%2C11.516667");
    assert(link.size()<100);
    for(unsigned char c:link)assert(c>=32 && c<127);
    value.fixAgeMs=30000;assert(deck::gpsMapsLink(value,link));
    value.fixAgeMs=30001;assert(!deck::gpsMapsLink(value,link) && link.empty());
    value.fixAgeMs=0;value.suspended=true;assert(!deck::gpsMapsLink(value,link));value.suspended=false;
    for(double bad:{91.0,-91.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){value.latitude=bad;assert(!deck::gpsMapsLink(value,link));}
    value.latitude=-90;value.longitude=180;assert(deck::gpsMapsLink(value,link));value.longitude=180.1;assert(!deck::gpsMapsLink(value,link));
    assert(feed(parser,frame("GNGST,123520.00,1,1,1,0,3,4,5"),2200));
    assert(parser.snapshot(2200).horizontalAccuracyMeters==5);
    assert(parser.snapshot(32201).horizontalAccuracyMeters<0 && !parser.snapshot(32201).fix);
    // A valid receiver sentence explicitly reporting no fix revokes sharing.
    assert(feed(parser,frame("GNRMC,123521.00,V,,,,,,,161026,,,N"),2300));
    assert(!parser.snapshot(2300).fix);
    for(const char* mode:{"E","M","S","N","Z"}){
        assert(feed(parser,frame(std::string("GNRMC,123521,A,4807,N,01131,E,0,0,161026,,,")+mode),2301));
        assert(!parser.snapshot(2301).fix);
    }
    for(unsigned quality:{6u,7u,8u}){
        assert(feed(parser,frame("GNGGA,123522,4807,N,01131,E,"+std::to_string(quality)+",08,0.9,1,M,,M,,"),2400));
        assert(!parser.snapshot(2400).fix);
    }
    assert(feed(parser,frame("GNGGA,123522,4807,N,01131,E,1,08,0.9,1,M,,M,,"),33002));
    assert(parser.snapshot(33002).fix && !parser.snapshot(33002).utc[0]);
    auto bad=frame("GNGGA,123522.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");bad[9]='9';assert(!feed(parser,bad,2400));
    for(const char* body:{"GNGGA,123519,9060.0,N,01131.0,E,1,08,0.9,1,M,,M,,","GNGGA,123519,4807,N,18100,E,1,08,0.9,1,M,,M,,","GNRMC,246000,A,4807,N,01131,E,0,0,161026,,,A","GNRMC,123519,A,4807,N,01131,E,0,0,310226,,,A"})assert(!feed(parser,frame(body),2500));
    parser.reset();assert(!parser.snapshot(0).fix && parser.snapshot(0).fixAgeMs==UINT32_MAX);
    assert(!feed(parser,"$"+std::string(1000,'x')+"\r\n",0));
    assert(feed(parser,frame("GPRMC,123519,A,3456.0,S,12345.0,W,0,0,161026,,,A"),UINT32_MAX-100));
    value=parser.snapshot(100);assert(value.fix && value.fixAgeMs==201 && value.latitude<0 && value.longitude<0);
    std::mt19937 random(0x475053);
    for(unsigned n=0;n<6000;++n){std::string bytes(random()%256,' ');for(char& c:bytes)c=char(random()%256);feed(parser,bytes,n);auto snapshot=parser.snapshot(n);assert(!snapshot.fix || (std::isfinite(snapshot.latitude)&&std::isfinite(snapshot.longitude)));}
    std::cout<<"PASS bounded NMEA/checksums/fix/time/HDOP/GST, stale/off/invalid sharing rejection, Maps URL, wraparound and6000 malformed inputs\n";
}
