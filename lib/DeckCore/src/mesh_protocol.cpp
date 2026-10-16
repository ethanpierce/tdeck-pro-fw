// SPDX-License-Identifier: GPL-3.0-only
// Meshtastic wire-format adaptation; pinned upstream sources are listed in
// mesh_protocol.h and docs/MESH.md. No upstream runtime/framework is imported.
#include "mesh_protocol.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace deck {
namespace {
uint32_t read32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
void write32(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
bool name(const char* p,size_t capacity,bool empty=false){
    const size_t n=strnlen(p,capacity);if(n==capacity || (!empty && !n))return false;
    for(size_t i=0;i<n;++i)if(uint8_t(p[i])<32 || uint8_t(p[i])>126)return false;
    return true;
}
bool utf8(const uint8_t* p,size_t n){
    for(size_t i=0;i<n;){const uint8_t c=p[i++];if(c<0x80){if((c<32 && c!='\n' && c!='\r' && c!='\t') || c==127)return false;continue;}
        unsigned continuation;uint32_t value,minimum;
        if(c>=0xc2 && c<=0xdf){continuation=1;value=c&31;minimum=0x80;}
        else if(c>=0xe0 && c<=0xef){continuation=2;value=c&15;minimum=0x800;}
        else if(c>=0xf0 && c<=0xf4){continuation=3;value=c&7;minimum=0x10000;}
        else return false;
        if(continuation>n-i)return false;
        while(continuation--){if((p[i]&0xc0)!=0x80)return false;value=(value<<6)|(p[i++]&63);}
        if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return false;
    }return true;
}
struct Writer {
    MeshBuffer& out;size_t limit;
    bool byte(uint8_t n){if(out.size>=limit)return false;out.bytes[out.size++]=n;return true;}
    bool varint(uint32_t n){do{uint8_t b=n&127;n>>=7;if(!byte(b|(n?128:0)))return false;}while(n);return true;}
    bool number(unsigned field,uint32_t n){return varint(field<<3) && varint(n);}
    bool bytes(unsigned field,const uint8_t* p,size_t n){if(n>limit-out.size || !varint((field<<3)|2) || !varint(uint32_t(n)) || n>limit-out.size)return false;if(n)std::memcpy(out.bytes.data()+out.size,p,n);out.size+=n;return true;}
};
struct Reader {
    const uint8_t* bytes;size_t size,pos=0;
    bool varint(uint64_t& out){out=0;for(unsigned n=0;n<10;++n){if(pos>=size)return false;const uint8_t b=bytes[pos++];if(n==9 && b>1)return false;out|=uint64_t(b&127)<<(7*n);if(!(b&128))return true;}return false;}
    bool key(unsigned& field,unsigned& wire){uint64_t key;if(!varint(key) || key>0xffffffffU || (key>>3)==0)return false;field=unsigned(key>>3);wire=unsigned(key&7);return true;}
    bool blob(const uint8_t*& data,size_t& length){uint64_t n;if(!varint(n) || n>size-pos)return false;length=size_t(n);data=bytes+pos;pos+=length;return true;}
    bool skip(unsigned wire){uint64_t value;const uint8_t* data;size_t length;
        if(wire==0)return varint(value);
        if(wire==2)return blob(data,length);
        const size_t n=wire==1?8:wire==5?4:0;if(!n || n>size-pos)return false;pos+=n;return true;
    }
};
bool copyName(const uint8_t* bytes,size_t size,char* out,size_t capacity){
    if(size>=capacity || !utf8(bytes,size))return false;
    // Names cannot inject terminal/control commands into diagnostics or UI.
    for(size_t i=0;i<size;++i)if(bytes[i]<32 || bytes[i]==127)return false;
    if(size)std::memcpy(out,bytes,size);
    out[size]=0;
    return true;
}
}
bool validMeshOptions(const MeshOptions& o){
    if(o.enabled>1 || uint8_t(o.region)>uint8_t(MeshRegion::US) || uint8_t(o.preset)>uint8_t(MeshPreset::ShortFast) || o.hopLimit>3 || o.frequencySlot>104 || (o.keyBytes!=16 && o.keyBytes!=32) || o.reserved)return false;
    for(auto byte:o.reserved2)if(byte)return false;
    if(o.keyBytes==16)for(unsigned i=16;i<32;++i)if(o.psk[i])return false;
    return name(o.channel,sizeof(o.channel)) && name(o.longName,sizeof(o.longName)) && name(o.shortName,sizeof(o.shortName));
}
bool meshPublicKey(const MeshOptions& o){return o.keyBytes==16 && !std::memcmp(o.psk,MeshDefaultKey,16);}
const char* meshPresetName(MeshPreset p){switch(p){case MeshPreset::LongFast:return "LongFast";case MeshPreset::MediumFast:return "MediumFast";case MeshPreset::ShortFast:return "ShortFast";}return "Invalid";}
float meshBandwidthKhz(MeshPreset p){return uint8_t(p)<=uint8_t(MeshPreset::ShortFast)?250.0f:0;}
uint8_t meshSpreadingFactor(MeshPreset p){switch(p){case MeshPreset::LongFast:return 11;case MeshPreset::MediumFast:return 9;case MeshPreset::ShortFast:return 7;}return 0;}
uint16_t meshFrequencySlot(const MeshOptions& o){
    if(!validMeshOptions(o) || o.region!=MeshRegion::US)return 0;
    if(o.frequencySlot)return o.frequencySlot;
    uint32_t hash=5381;for(const unsigned char* p=reinterpret_cast<const unsigned char*>(o.channel);*p;++p)hash=hash*33U+*p;
    return uint16_t(hash%104U+1U);
}
float meshFrequencyMhz(const MeshOptions& o){const auto slot=meshFrequencySlot(o);return slot?902.125f+float(slot-1)*0.25f:0;}
uint8_t meshChannelHash(const MeshOptions& o){
    if(!validMeshOptions(o))return 0;
    uint8_t value=0;
    for(const char* p=o.channel;*p;++p)value^=uint8_t(*p);
    for(unsigned i=0;i<o.keyBytes;++i)value^=o.psk[i];
    return value;
}
void meshNonce(uint32_t from,uint32_t id,uint8_t nonce[16]){std::memset(nonce,0,16);write32(nonce,id);write32(nonce+8,from);}
bool meshEncodeHeader(const MeshHeader& h,uint8_t* bytes,size_t capacity){
    if(!bytes || capacity<MeshHeaderSize || !h.to || !h.from || h.from==MeshBroadcast || !h.id || h.hopLimit>7 || h.hopStart>7)return false;
    write32(bytes,h.to);write32(bytes+4,h.from);write32(bytes+8,h.id);
    bytes[12]=h.hopLimit|(h.wantAck?8:0)|(h.viaMqtt?16:0)|(h.hopStart<<5);bytes[13]=h.channel;bytes[14]=h.nextHop;bytes[15]=h.relay;return true;
}
bool meshDecodeHeader(const uint8_t* bytes,size_t size,MeshHeader& h){
    h={};if(!bytes || size<=MeshHeaderSize || size>MeshFrameLimit)return false;
    h.to=read32(bytes);h.from=read32(bytes+4);h.id=read32(bytes+8);h.hopLimit=bytes[12]&7;h.hopStart=bytes[12]>>5;
    h.wantAck=bytes[12]&8;h.viaMqtt=bytes[12]&16;h.channel=bytes[13];h.nextHop=bytes[14];h.relay=bytes[15];
    return h.to && h.from && h.from!=MeshBroadcast && h.id;
}
bool meshEncodeData(uint32_t port,const uint8_t* payload,size_t size,bool wantResponse,MeshBuffer& out){
    const uintptr_t source=reinterpret_cast<uintptr_t>(payload),destination=reinterpret_cast<uintptr_t>(out.bytes.data());
    const bool alias=source>=destination && source<destination+out.bytes.size();
    out={};if(alias || !port || !payload || !size || size>MeshPayloadLimit)return false;
    Writer w{out,MeshFrameLimit-MeshHeaderSize};
    // Explicit bitfield=0 means no consent to MQTT forwarding. want_response
    // retains its schema field for compatibility with older 2.x peers.
    if(!w.number(1,port) || !w.bytes(2,payload,size) || (wantResponse && !w.number(3,1)) || !w.number(9,0)){out={};return false;}return true;
}
bool meshDecodeData(const uint8_t* bytes,size_t size,MeshData& out){
    out={};if(!bytes || !size || size>MeshFrameLimit-MeshHeaderSize)return false;
    Reader r{bytes,size};bool port=false,payload=false,response=false,bitfield=false;
    while(r.pos<size){unsigned field,wire;if(!r.key(field,wire))return false;
        if(field==1){uint64_t n;if(port || wire || !r.varint(n) || !n || n>0xffffffffU)return false;out.port=uint32_t(n);port=true;}
        else if(field==2){const uint8_t* p;size_t n;if(payload || wire!=2 || !r.blob(p,n) || n>out.payload.size())return false;std::memcpy(out.payload.data(),p,n);out.size=n;payload=true;}
        else if(field==3){uint64_t n;if(response || wire || !r.varint(n) || n>1)return false;out.wantResponse=n;response=true;}
        else if(field==9){uint64_t n;if(bitfield || wire || !r.varint(n) || n>0xffffffffU)return false;bitfield=true;}
        else if(!r.skip(wire))return false;
    }return port && payload && out.size;
}
bool meshEncodeUser(uint32_t node,const MeshOptions& o,MeshBuffer& out){
    out={};if(!node || node==MeshBroadcast || !validMeshOptions(o))return false;
    char id[10];snprintf(id,sizeof(id),"!%08lx",static_cast<unsigned long>(node));Writer w{out,MeshPayloadLimit};
    // CLIENT_MUTE accurately advertises this non-relaying leaf role. Private
    // PKI DMs are unsupported, so mark the node unavailable for direct chats.
    if(!w.bytes(1,reinterpret_cast<const uint8_t*>(id),9) || !w.bytes(2,reinterpret_cast<const uint8_t*>(o.longName),std::strlen(o.longName)) || !w.bytes(3,reinterpret_cast<const uint8_t*>(o.shortName),std::strlen(o.shortName)) || !w.number(5,102) || !w.number(7,1) || !w.number(9,1)){out={};return false;}return true;
}
bool meshDecodeUser(const uint8_t* bytes,size_t size,MeshUser& out){
    out={};if(!bytes || !size || size>MeshPayloadLimit)return false;
    Reader r{bytes,size};bool longName=false,shortName=false,hardware=false;
    while(r.pos<size){unsigned field,wire;if(!r.key(field,wire))return false;
        if(field==2 || field==3){const uint8_t* p;size_t n;bool& seen=field==2?longName:shortName;
            if(seen || wire!=2 || !r.blob(p,n) || !copyName(p,n,field==2?out.longName:out.shortName,field==2?sizeof(out.longName):sizeof(out.shortName)))return false;
            seen=true;}
        else if(field==5){uint64_t n;if(hardware || wire || !r.varint(n) || n>0xffffffffU)return false;out.hardwareModel=uint32_t(n);hardware=true;}
        else if(!r.skip(wire))return false;
    }return (longName && out.longName[0]) || (shortName && out.shortName[0]);
}
bool meshText(const MeshData& data,char* output,size_t capacity){
    if(!output || data.port!=MeshTextPort || !data.size || data.size>data.payload.size() || data.size>=capacity || !utf8(data.payload.data(),data.size))return false;
    std::memcpy(output,data.payload.data(),data.size);output[data.size]=0;return true;
}
bool MeshDuplicateCache::remember(uint32_t from,uint32_t id){
    if(!from || from==MeshBroadcast || !id)return false;
    for(const auto& e:entries_)if(e.from==from && e.id==id)return false;
    entries_[next_]={from,id};next_=(next_+1)%entries_.size();return true;
}
}
