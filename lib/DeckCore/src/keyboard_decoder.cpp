#include "keyboard_decoder.h"
namespace deck {
char KeyboardDecoder::feed(uint8_t event) {
    unsigned number=event & 127;
    bool down=event & 128;
    if(number==30){alt_=down;return 0;}
    if(number==35){leftShift_=down;return 0;}
    if(number==31){rightShift_=down;return 0;}
    if(!down || number==0 || number>35) return 0;
    if(number==32) return 27;
    if(number==33) return ' ';
    if(number==34) return alt_?'0':0;
    static const char letters[30]={
        'p','o','i','u','y','t','r','e','w','q',
        '\b','l','k','j','h','g','f','d','s','a',
        '\r','$','m','n','b','v','c','x','z',0
    };
    static const char alternate[30]={
        '@','+','-','_',')','(','3','2','1','#',
        '\b','"','\'',';',':','/','6','5','4','*',
        '\r','$','.',',','!','?','9','8','7',0
    };
    char c=alt_?alternate[number-1]:letters[number-1];
    if(!alt_ && (leftShift_ || rightShift_) && c>='a' && c<='z') c-=32;
    return c;
}
}
