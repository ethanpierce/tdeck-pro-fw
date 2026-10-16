#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <deque>
#include <functional>
namespace platform {
struct FakeUart {
    std::deque<char> rx;
    std::function<void(std::string)> onWrite;
    void setRxBufferSize(size_t) {}
    void begin(int,int,int) {}
    int available() {return !rx.empty();}
    int read() {char c=rx.front();rx.pop_front();return c;}
    size_t write(const uint8_t* p,size_t n) {if(onWrite)onWrite(std::string(reinterpret_cast<const char*>(p),n));return n;}
    void feed(std::string s) {for(char c:s)rx.push_back(c);}
};
extern FakeUart modemUart;
uint32_t millis();
}
