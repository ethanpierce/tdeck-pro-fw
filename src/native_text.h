#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>

// UI text convenience with standard-library storage. No framework runtime,
// allocation hooks, or Arduino headers are involved.
class Text : public std::string {
public:
    using std::string::string;
    using std::string::operator=;
    Text()=default;
    Text(const std::string& value):std::string(value){}
    Text(std::string&& value):std::string(std::move(value)){}
    template<class T,std::enable_if_t<std::is_arithmetic_v<T>,int> =0>
    explicit Text(T value):std::string(std::to_string(value)){}
    bool isEmpty()const{return empty();}
    Text substring(size_t first,size_t last=npos)const {
        if(first>=size())return {};
        if(last<first)std::swap(first,last);
        return substr(first,last==npos?npos:last-first);
    }
    int indexOf(char value,size_t first=0)const {const auto at=find(value,first);return at==npos?-1:int(at);}
    int indexOf(const char* value,size_t first=0)const {const auto at=find(value,first);return at==npos?-1:int(at);}
    int lastIndexOf(char value,size_t last=npos)const {const auto at=rfind(value,last);return at==npos?-1:int(at);}
    bool startsWith(const char* prefix)const{return compare(0,std::char_traits<char>::length(prefix),prefix)==0;}
    void remove(size_t first,size_t count=npos){if(first<size())erase(first,count);}
    long toInt()const{return std::strtol(c_str(),nullptr,10);}
    void trim(){
        const auto first=find_first_not_of(" \t\r\n\f\v");
        if(first==npos){clear();return;}
        erase(find_last_not_of(" \t\r\n\f\v")+1);erase(0,first);
    }
    friend Text operator+(const Text& a,const Text& b){Text value=a;value.append(b);return value;}
    friend Text operator+(const Text& a,const char* b){Text value=a;value.append(b);return value;}
    friend Text operator+(const char* a,const Text& b){Text value(a);value.append(b);return value;}
    friend Text operator+(const Text& a,char b){Text value=a;value.push_back(b);return value;}
    template<class T,std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T,char>,int> =0>
    friend Text operator+(const Text& a,T b){Text value=a;value.append(std::to_string(b));return value;}
};
