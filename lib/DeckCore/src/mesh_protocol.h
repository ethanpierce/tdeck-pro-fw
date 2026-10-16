// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace deck {
// Interoperability target: Meshtastic firmware 54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb,
// protobufs 6b1ded439633cd03d4af85b44231b91d1d106278. This is a bounded leaf client.
enum class MeshRegion:uint8_t {Unset=0,US=1};
enum class MeshPreset:uint8_t {LongFast=0,MediumFast=1,ShortFast=2};
inline constexpr uint8_t MeshDefaultKey[16]={0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01};
// Stable 100-byte POD, wrapped/versioned/checksummed separately by settings.
// Default key is public and provides no privacy. No transmission is permitted
// until a region is explicitly selected and enabled is saved by the user.
struct MeshOptions {
    uint8_t enabled=0;
    MeshRegion region=MeshRegion::Unset;
    MeshPreset preset=MeshPreset::LongFast;
    uint8_t hopLimit=3;
    uint16_t frequencySlot=0; // 0 = upstream channel-name-derived frequency slot.
    uint8_t keyBytes=16,reserved=0;
    char channel[12]="LongFast",longName[40]="Handheld",shortName[5]="HH";
    uint8_t reserved2[3]={};
    uint8_t psk[32]={0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01};
};
static_assert(sizeof(MeshOptions)==100,"Keep the native mesh settings blob layout stable");
constexpr size_t MeshFrameLimit=255,MeshHeaderSize=16,MeshPayloadLimit=233,MeshTextLimit=233,MeshSendTextLimit=160;
constexpr uint32_t MeshBroadcast=0xffffffffU,MeshTextPort=1,MeshNodeInfoPort=4;
struct MeshBuffer {std::array<uint8_t,MeshFrameLimit> bytes{};size_t size=0;};
struct MeshHeader {
    uint32_t to=MeshBroadcast,from=0,id=0;
    uint8_t hopLimit=3,hopStart=3,channel=0,nextHop=0,relay=0;
    bool wantAck=false,viaMqtt=false;
};
struct MeshData {
    uint32_t port=0;bool wantResponse=false;
    std::array<uint8_t,MeshPayloadLimit> payload{};size_t size=0;
};
struct MeshUser {char longName[40]={},shortName[5]={};uint32_t hardwareModel=0;};
struct MeshNode {uint32_t id=0,lastSeen=0;int16_t rssi=0;MeshUser user{};};
bool validMeshOptions(const MeshOptions& options);
bool meshPublicKey(const MeshOptions& options);
const char* meshPresetName(MeshPreset preset);
float meshBandwidthKhz(MeshPreset preset);
uint8_t meshSpreadingFactor(MeshPreset preset);
uint16_t meshFrequencySlot(const MeshOptions& options);
float meshFrequencyMhz(const MeshOptions& options);
uint8_t meshChannelHash(const MeshOptions& options);
void meshNonce(uint32_t from,uint32_t id,uint8_t nonce[16]);
bool meshEncodeHeader(const MeshHeader& header,uint8_t* bytes,size_t capacity);
bool meshDecodeHeader(const uint8_t* bytes,size_t size,MeshHeader& header);
bool meshEncodeData(uint32_t port,const uint8_t* payload,size_t size,bool wantResponse,MeshBuffer& out);
bool meshDecodeData(const uint8_t* bytes,size_t size,MeshData& out);
bool meshEncodeUser(uint32_t node,const MeshOptions& options,MeshBuffer& out);
bool meshDecodeUser(const uint8_t* bytes,size_t size,MeshUser& out);
bool meshText(const MeshData& data,char* output,size_t capacity);
class MeshDuplicateCache {
public:
    bool remember(uint32_t from,uint32_t id); // false for duplicate or invalid ID.
    void clear(){entries_={};next_=0;}
private:
    struct Entry {uint32_t from=0,id=0;};
    std::array<Entry,64> entries_{};size_t next_=0;
};
}
