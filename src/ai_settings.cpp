#include "ai_settings.h"
#include <cstring>
void AiSettings::begin() {
    value_=deck::AiConfig{};persistent=prefs_.begin("deckai",false);if(!persistent)return;
    deck::AiConfig saved{};
    if(prefs_.getBytesLength("config")==sizeof(saved) && prefs_.getBytes("config",&saved,sizeof(saved))==sizeof(saved) &&
       deck::validAiConfig(saved) && saved.checksum==deck::aiConfigChecksum(saved))value_=saved;
}
bool AiSettings::save(deck::AiConfig value) {
    if(!persistent || !deck::validAiConfig(value))return false;
    value.checksum=deck::aiConfigChecksum(value);
    if(prefs_.putBytes("config",&value,sizeof(value))!=sizeof(value)){persistent=false;return false;}
    value_=value;return true;
}
bool AiSettings::set(Field field,const std::string& text) {
    auto value=value_;char* target=nullptr;size_t size=0;
    switch(field) {
    case Field::Endpoint:target=value.endpoint;size=sizeof(value.endpoint);break;
    case Field::Model:target=value.model;size=sizeof(value.model);break;
    case Field::Key:target=value.key;size=sizeof(value.key);break;
    }
    if(text.size()>=size || text.find('\0')!=std::string::npos)return false;
    // A provider change must not forward the previous provider's credential.
    // Persist both changes together; a failed write retains the old RAM record.
    if(field==Field::Endpoint && text!=value_.endpoint)std::memset(value.key,0,sizeof(value.key));
    std::memset(target,0,size);std::memcpy(target,text.data(),text.size());
    return save(value);
}
