#include "apn.h"
namespace deck {
bool validApn(const std::string& value) {
    if (value.empty() || value.size()>63) return false;
    bool labelStart=true;
    char previous=0;
    for (unsigned char c:value) {
        bool alnum=(c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');
        if (alnum) labelStart=false;
        else if (c=='-') {if(labelStart) return false;}
        else if (c=='.') {
            if(labelStart || previous=='-') return false;
            labelStart=true;
        } else return false;
        previous=char(c);
    }
    return !labelStart && previous!='-';
}
}
