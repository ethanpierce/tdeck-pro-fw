#pragma once
// In-memory ESP-IDF NVS boundary for exercising the production blob adapter.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using esp_err_t=int;
using nvs_handle_t=uint32_t;
constexpr esp_err_t ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NVS_INVALID_LENGTH=2, ESP_FAIL=3;
constexpr int NVS_READONLY=0, NVS_READWRITE=1;
namespace fakeNvs {
inline std::map<std::string,std::vector<uint8_t>> blobs;
inline std::map<nvs_handle_t,std::string> handles;
inline bool failWrites=false,failCommit=false;
inline unsigned commits=0,opens=0;
inline void clear() {blobs.clear();failWrites=false;failCommit=false;commits=opens=0;}
}
inline esp_err_t nvs_open(const char* name,int,nvs_handle_t* handle) {
    *handle=++fakeNvs::opens;fakeNvs::handles[*handle]=std::string(name)+"/";return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) {fakeNvs::handles.erase(handle);}
inline esp_err_t nvs_get_blob(nvs_handle_t handle,const char* key,void* out,size_t* length) {
    auto found=fakeNvs::blobs.find(fakeNvs::handles.at(handle)+key);
    if(found==fakeNvs::blobs.end())return ESP_ERR_NVS_NOT_FOUND;
    if(out && *length<found->second.size())return ESP_ERR_NVS_INVALID_LENGTH;
    if(out)std::memcpy(out,found->second.data(),found->second.size());
    *length=found->second.size();return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle,const char* key,const void* data,size_t length) {
    if(fakeNvs::failWrites)return ESP_FAIL;
    auto bytes=static_cast<const uint8_t*>(data);
    fakeNvs::blobs[fakeNvs::handles.at(handle)+key]=std::vector<uint8_t>(bytes,bytes+length);return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t) {++fakeNvs::commits;return fakeNvs::failCommit?ESP_FAIL:ESP_OK;}
