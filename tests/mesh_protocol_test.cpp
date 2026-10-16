#include <mesh_protocol.h>
#include "fixtures/meshtastic_v2_7.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>
using namespace deck;
template<size_t N> void equals(const MeshBuffer& buffer,const uint8_t(&expected)[N]){assert(buffer.size==N && std::equal(expected,expected+N,buffer.bytes.begin()));}
int main(){
    MeshOptions options;assert(sizeof(options)==100 && validMeshOptions(options) && !options.enabled && options.region==MeshRegion::Unset && meshPublicKey(options));
    assert(meshFrequencyMhz(options)==0 && meshFrequencySlot(options)==0);
    options.region=MeshRegion::US;assert(meshFrequencySlot(options)==20 && meshFrequencyMhz(options)==906.875f && meshChannelHash(options)==8);
    assert(meshSpreadingFactor(MeshPreset::LongFast)==11 && meshSpreadingFactor(MeshPreset::MediumFast)==9 && meshSpreadingFactor(MeshPreset::ShortFast)==7 && meshBandwidthKhz(MeshPreset::ShortFast)==250);
    options.frequencySlot=104;assert(meshFrequencyMhz(options)==927.875f);options.frequencySlot=105;assert(!validMeshOptions(options));options.frequencySlot=0;
    auto invalid=options;invalid.hopLimit=4;assert(!validMeshOptions(invalid));invalid=options;invalid.enabled=2;assert(!validMeshOptions(invalid));
    invalid=options;std::memset(invalid.channel,'x',sizeof(invalid.channel));assert(!validMeshOptions(invalid));
    invalid=options;invalid.keyBytes=17;assert(!validMeshOptions(invalid));invalid=options;invalid.psk[16]=1;assert(!validMeshOptions(invalid));
    invalid=options;invalid.keyBytes=32;assert(validMeshOptions(invalid) && !meshPublicKey(invalid));invalid=options;invalid.reserved2[1]=1;assert(!validMeshOptions(invalid));
    MeshHeader header;header.from=0x11223344;header.id=0x55667788;header.channel=8;header.relay=0x44;
    uint8_t raw[255]{};assert(meshEncodeHeader(header,raw,sizeof(raw)) && !std::memcmp(raw,mesh_fixture::Header,sizeof(mesh_fixture::Header)));
    MeshHeader decoded;std::memcpy(raw,mesh_fixture::HelloFrame128,sizeof(mesh_fixture::HelloFrame128));assert(meshDecodeHeader(raw,sizeof(mesh_fixture::HelloFrame128),decoded));
    assert(decoded.to==MeshBroadcast && decoded.from==header.from && decoded.id==header.id && decoded.hopStart==3 && decoded.hopLimit==3 && decoded.channel==8 && !decoded.wantAck);
    assert(!meshDecodeHeader(raw,16,decoded) && !meshDecodeHeader(raw,256,decoded));raw[4]=raw[5]=raw[6]=raw[7]=0;assert(!meshDecodeHeader(raw,20,decoded));
    uint8_t nonce[16];meshNonce(header.from,header.id,nonce);assert(!std::memcmp(nonce,mesh_fixture::Nonce,16));
    MeshBuffer buffer;const uint8_t hello[]={'H','e','l','l','o'};
    assert(meshEncodeData(MeshTextPort,hello,sizeof(hello),false,buffer));equals(buffer,mesh_fixture::HelloData);
    MeshData data;assert(meshDecodeData(mesh_fixture::HelloData,sizeof(mesh_fixture::HelloData),data));char text[234];assert(meshText(data,text,sizeof(text)) && !std::strcmp(text,"Hello"));
    assert(meshEncodeUser(header.from,options,buffer));equals(buffer,mesh_fixture::UserPayload);
    MeshUser user;assert(meshDecodeUser(mesh_fixture::UserPayload,sizeof(mesh_fixture::UserPayload),user));assert(!std::strcmp(user.longName,"Handheld") && !std::strcmp(user.shortName,"HH") && user.hardwareModel==102);
    assert(meshEncodeData(MeshNodeInfoPort,buffer.bytes.data(),buffer.size,false,buffer)==false); // Input/output alias is not an API-supported source.
    MeshBuffer payload;assert(meshEncodeUser(header.from,options,payload));assert(meshEncodeData(MeshNodeInfoPort,payload.bytes.data(),payload.size,false,buffer));equals(buffer,mesh_fixture::NodeInfoData);
    std::vector<uint8_t> extended(mesh_fixture::HelloData,mesh_fixture::HelloData+sizeof(mesh_fixture::HelloData));extended.insert(extended.end(),{0xa5,0x01,1,2,3,4});assert(meshDecodeData(extended.data(),extended.size(),data));
    const std::vector<std::vector<uint8_t>> bad={{0},{0x08,1},{0x08,1,0x12,5,'x'},{0x08,1,0x08,1,0x12,1,'x'},{0x0a,1,1,0x12,1,'x'},{0x08,1,0x12,1,'x',0x18,2},{0x08,1,0x12,1,'x',0x1b}};
    for(const auto& bytes:bad)assert(!meshDecodeData(bytes.data(),bytes.size(),data));
    data={};data.port=1;data.payload[0]=0;data.size=1;assert(!meshText(data,text,sizeof(text)));data.payload[0]='x';data.size=234;assert(!meshText(data,text,4096));
    const uint8_t smile[]={0xf0,0x9f,0x98,0x80};assert(meshEncodeData(1,smile,sizeof(smile),false,buffer) && meshDecodeData(buffer.bytes.data(),buffer.size,data) && meshText(data,text,sizeof(text)));
    data.payload[0]=0xc0;data.payload[1]=0xaf;data.size=2;assert(!meshText(data,text,sizeof(text)));
    std::array<uint8_t,MeshPayloadLimit> maximum{};maximum.fill('x');assert(meshEncodeData(1,maximum.data(),160,false,buffer));assert(buffer.size+MeshHeaderSize<=255);
    assert(!meshEncodeData(1,maximum.data(),maximum.size(),false,buffer)); // Explicit MQTT opt-out must still fit the 255-byte RF limit.
    MeshDuplicateCache duplicates;for(unsigned i=1;i<=64;++i)assert(duplicates.remember(i,i));assert(!duplicates.remember(1,1));assert(duplicates.remember(65,65) && duplicates.remember(1,1));assert(!duplicates.remember(0,1));duplicates.clear();assert(duplicates.remember(1,1));
    std::mt19937 random(0x4d455348);unsigned accepted=0;
    for(unsigned trial=0;trial<10000;++trial){std::array<uint8_t,300> bytes{};const size_t size=random()%bytes.size();for(size_t n=0;n<size;++n)bytes[n]=uint8_t(random());
        if(meshDecodeData(bytes.data(),size,data)){assert(data.size<=233);++accepted;meshText(data,text,sizeof(text));}
        meshDecodeUser(bytes.data(),size,user);meshDecodeHeader(bytes.data(),size,decoded);
    }
    std::cout<<"PASS mesh official-schema headers/data/user/nonce fixtures, region/key bounds, UTF-8, duplicate eviction and 10000 malformed frames ("<<accepted<<" valid random payloads)\n";
}
