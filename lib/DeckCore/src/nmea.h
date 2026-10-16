#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace deck {
struct GpsSnapshot {
    bool enabled=false,preferredEnabled=false,suspended=false,fix=false;
    double latitude=0,longitude=0;
    float altitudeMeters=0,hdop=-1,horizontalAccuracyMeters=-1;
    uint8_t satellites=0,quality=0;
    uint32_t fixAgeMs=UINT32_MAX,sentenceAgeMs=UINT32_MAX;
    char utc[21]{};
    const char* status="Off";
};
class NmeaParser {
public:
    bool feed(char byte,uint32_t now);
    void reset();
    GpsSnapshot snapshot(uint32_t now)const;
private:
    std::array<char,128> line_{};
    size_t used_=0;
    bool collecting_=false,cr_=false,seen_=false,position_=false,valid_=false,accuracySeen_=false,detailsSeen_=false,utcSeen_=false;
    uint32_t sentenceAt_=0,fixAt_=0,accuracyAt_=0,detailsAt_=0,utcAt_=0;
    GpsSnapshot value_{};
    bool parse(uint32_t now);
};
// Explicit sharing only: no network request, key, persistence or SMS send.
bool gpsMapsLink(const GpsSnapshot& value,std::string& out);
}
