#pragma once
// Host fake for the documented Preferences byte-blob API; no flash is simulated.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace fakeNvs {
inline std::map<std::string,std::vector<uint8_t>> blobs;
inline bool failWrites=false;
inline void clear() {blobs.clear();failWrites=false;}
}
class Preferences {
public:
    bool begin(const char* name,bool) {prefix_=std::string(name)+"/";return true;}
    size_t getBytesLength(const char* name) const {
        auto found=fakeNvs::blobs.find(prefix_+name);
        return found==fakeNvs::blobs.end()?0:found->second.size();
    }
    size_t getBytes(const char* name,void* out,size_t size) const {
        auto found=fakeNvs::blobs.find(prefix_+name);
        if(found==fakeNvs::blobs.end() || found->second.size()>size) return 0;
        std::memcpy(out,found->second.data(),found->second.size());
        return found->second.size();
    }
    size_t putBytes(const char* name,const void* data,size_t size) {
        if(fakeNvs::failWrites) return 0;
        auto bytes=static_cast<const uint8_t*>(data);
        fakeNvs::blobs[prefix_+name]=std::vector<uint8_t>(bytes,bytes+size);
        return size;
    }
private:
    std::string prefix_;
};
