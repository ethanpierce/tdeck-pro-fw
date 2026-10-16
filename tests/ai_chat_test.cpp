#include <ai_protocol.h>
#include <ai_models.h>
#include "ai_settings.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
namespace {
std::string response(const std::string& content,const std::string& finish="stop") {
    return "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":"+content+"},\"finish_reason\":\""+finish+"\"}],\"unused\":{\"a\":[null,true,false,1,-2.5e+3]}}";
}
std::string repeated(const std::string& text,size_t count) {
    std::string result;for(size_t n=0;n<count;++n)result+=text;return result;
}
}
int main() {
    using namespace deck;
    assert(validAiEndpoint("https://api.openai.com/v1"));
    assert(validAiEndpoint("https://provider.example:8443/api/v1/"));
    assert(validAiEndpoint("https://192.0.2.1/v1"));
    for(const auto* value:{"http://host/v1","https://","https://user:pass@host/v1","https://host/v1?key=a","https://host/v1#x","https://host\\evil/v1","https://host/\r\nInjected:x","https://host:0","https://host:65536","https://host:abc","https://.host/v1","https://-host/v1"})assert(!validAiEndpoint(value));
    assert(aiCompletionUrl("https://api.openai.com/v1/")=="https://api.openai.com/v1/chat/completions");
    assert(aiCompletionUrl("https://provider.example/chat/completions")=="https://provider.example/chat/completions");
    assert(aiCompletionUrl("http://host").empty());
    std::string request;
    assert(aiRequest("provider/model",{{false,"Earlier"},{true,"Reply"}},"Hello \"API\"\n\\",request));
    assert(request=="{\"model\":\"provider/model\",\"messages\":[{\"role\":\"system\",\"content\":\"You are answering on a small 240x320 e-paper handheld. Keep every reply within 500 Unicode characters total, including spaces and line breaks. Be concise. Use plain text; avoid tables and long lists.\"},{\"role\":\"user\",\"content\":\"Earlier\"},{\"role\":\"assistant\",\"content\":\"Reply\"},{\"role\":\"user\",\"content\":\"Hello \\\"API\\\"\\u000a\\\\\"}],\"stream\":false}");
    assert(request.find("\"role\":\"system\"")<request.find("Earlier") && request.find("Earlier")<request.find("Reply") && request.find("Reply")<request.find("Hello"));
    assert(aiRequest("gpt-5.6-luna",{},"Hi",request,true) && request.find("\"max_completion_tokens\":256")!=std::string::npos && request.find("\"reasoning_effort\":\"none\"")!=std::string::npos);
    assert(aiRequest("gpt-5.6-luna",{},"Hi",request) && request.find("max_completion_tokens")==std::string::npos);
    assert(AiModelPresetCount==4);
    for(const auto& preset:AiModelPresets) {
        assert(boundedOpenAiModel(preset.id));
        assert(aiRequest(preset.id,{},"Hi",request,true) && request.find("\"max_completion_tokens\":256")!=std::string::npos && request.find("\"reasoning_effort\":\"none\"")!=std::string::npos);
        assert(aiRequest(preset.id,{},"Hi",request,false) && request.find("max_completion_tokens")==std::string::npos && request.find("reasoning_effort")==std::string::npos);
    }
    assert(!boundedOpenAiModel("gpt-5.6-luna-custom") && !boundedOpenAiModel("gpt-4.1"));
    assert(aiRequest("other-model",{},"Hi",request,true) && request.find("reasoning_effort")==std::string::npos);
    assert(!aiRequest("",{},"Hello",request) && request.empty());
    assert(!aiRequest("model",{}," \n",request));
    assert(!aiRequest("model",{},std::string(AiPromptLimit+1,'x'),request));
    assert(!aiRequest("model",std::vector<AiMessage>(AiHistoryLimit+1,{false,"x"}),"x",request));
    assert(!aiRequest("model",{{true,std::string(AiReplyLimit+1,'x')}},"x",request));
    assert(aiRequest("model",{},"\xc3\xa9",request));
    assert(!aiRequest("model",{},"\xc0\xaf",request));
    auto reply=aiResponse(200,response("\"Hello\\nworld \\u00e9 \\ud83d\\ude03\""));
    assert(reply.ok && !reply.truncated && reply.text=="Hello\nworld \xc3\xa9 \xf0\x9f\x98\x83");
    assert(aiResponse(200,response("\"bounded\"","length")).truncated);
    reply=aiResponse(200,response("\""+std::string(500,'x')+"\""));
    assert(reply.ok && !reply.truncated && reply.text.size()==500);
    reply=aiResponse(200,response("\""+std::string(501,'x')+"\""));
    assert(reply.ok && reply.truncated && reply.text==std::string(500,'x'));
    reply=aiResponse(200,response("\""+std::string(499,'x')+" y\""));
    assert(reply.ok && reply.truncated && reply.text.size()==500 && reply.text.back()==' ');
    reply=aiResponse(200,response("\""+std::string(499,'x')+"\\ny\""));
    assert(reply.ok && reply.truncated && reply.text.size()==500 && reply.text.back()=='\n');
    reply=aiResponse(200,response('"'+repeated("\xc3\xa9",500)+'"'));
    assert(reply.ok && !reply.truncated && reply.text==repeated("\xc3\xa9",500));
    reply=aiResponse(200,response('"'+repeated("\xe4\xb8\xad",501)+'"'));
    assert(reply.ok && reply.truncated && reply.text==repeated("\xe4\xb8\xad",500));
    reply=aiResponse(200,response('"'+repeated("\\ud83d\\ude03",500)+'"'));
    assert(reply.ok && !reply.truncated && reply.text==repeated("\xf0\x9f\x98\x83",500) && reply.text.size()==2000);
    reply=aiResponse(200,response('"'+repeated("\xf0\x9f\x98\x83",501)+'"'));
    assert(reply.ok && reply.truncated && reply.text==repeated("\xf0\x9f\x98\x83",500) && reply.text.size()<=AiReplyLimit);
    reply=aiResponse(200,response("\""+std::string(499,'x')+"\xf0\x9f\x98\x83\xc3\xa9\""));
    assert(reply.ok && reply.truncated && reply.text==std::string(499,'x')+"\xf0\x9f\x98\x83");
    reply=aiResponse(200,response("\""+std::string(2047,'x')+"\xc3\xa9\""));
    assert(reply.ok && reply.truncated && reply.text.size()==500 && reply.text.size()<=AiReplyLimit);
    for(const auto& body:{"", "{}", "[]", "{\"choices\":null}", "{\"choices\":{\"0\":{\"message\":{\"content\":\"wrong\"}}}}", "{\"choices/0/message/content\":\"wrong\"}", "{\"choices\":[{\"message\":{\"content\":null}}]}", "{\"choices\":[{\"message\":{\"content\":\"a\",\"content\":\"b\"}}]}", "{\"x\":01}"})assert(!aiResponse(200,body).ok);
    for(const auto& content:{"\"\\ud800\"","\"\\udc00\"","\"\\ud800\\u0041\"","\"\\u0000\"","\"\\q\"","\"unclosed","\"\xc0\xaf\""})assert(!aiResponse(200,response(content)).ok);
    assert(!aiResponse(200,std::string(AiBodyLimit+1,'x')).ok);
    std::string deep="0";for(int n=0;n<18;++n)deep='['+deep+']';
    assert(!aiResponse(200,"{\"unused\":"+deep+",\"choices\":[{\"message\":{\"content\":\"x\"}}]}").ok);
    // Provider error bodies may echo Authorization; never put them on screen/log.
    for(int status:{-1,301,400,401,403,404,429,500}) {
        reply=aiResponse(status,"{\"error\":\"Bearer secret-key\"}");
        assert(!reply.ok && reply.text.empty() && reply.error.find("secret-key")==std::string::npos);
    }
    auto lines=aiDisplayLines({{false,"abcdef"},{true,"Hi\n\xc3\xa9\xf0\x9f\x98\x83"}},3);
    assert(lines==std::vector<std::string>({"You:","abc","def","","AI:","Hi","??"}));
    assert(aiDisplayLines({{false,"x"}},0).empty());
    assert(!lines.back().empty());
    assert(aiLastPageOffset({})==0 && aiLastPageOffset(lines,0)==0);
    std::vector<std::string> pageLines(16,"text");pageLines.emplace_back();assert(aiLastPageOffset(pageLines)==0);
    pageLines.insert(pageLines.begin(),"one more line");assert(aiLastPageOffset(pageLines)==16);
    pageLines=std::vector<std::string>(32,"text");pageLines.emplace_back();assert(aiLastPageOffset(pageLines)==16);
    // Config is isolated from Wi-Fi, SMS and existing settings records.
    fakeNvs::clear();fakeNvs::blobs["decksettings/options"]={1,2,3};fakeNvs::blobs["deckos/history"]={4,5,6};
    AiSettings saved;saved.begin();assert(saved.persistent && std::string(saved.values().model)=="gpt-5.6-luna");
    auto value=saved.values();std::strcpy(value.model,"test-model");std::strcpy(value.key,"test-key-never-real");
    assert(saved.save(value));AiSettings reboot;reboot.begin();assert(std::string(reboot.values().model)=="test-model" && std::string(reboot.values().key)=="test-key-never-real");
    assert(fakeNvs::blobs["decksettings/options"]==std::vector<uint8_t>({1,2,3}));assert(fakeNvs::blobs["deckos/history"]==std::vector<uint8_t>({4,5,6}));
    assert(!reboot.set(AiSettings::Field::Endpoint,"http://example.test/v1"));
    assert(reboot.set(AiSettings::Field::Endpoint,"https://api.openai.com/v1") && std::string(reboot.values().key)=="test-key-never-real");
    assert(reboot.set(AiSettings::Field::Model,"other-model") && std::string(reboot.values().key)=="test-key-never-real");
    fakeNvs::failWrites=true;
    assert(!reboot.set(AiSettings::Field::Endpoint,"https://example.test/v1") && std::string(reboot.values().endpoint)=="https://api.openai.com/v1" && std::string(reboot.values().key)=="test-key-never-real");
    fakeNvs::failWrites=false;reboot.begin();
    assert(reboot.set(AiSettings::Field::Endpoint,"https://example.test/v1") && !reboot.values().key[0]);
    AiSettings providerRestart;providerRestart.begin();assert(!providerRestart.values().key[0] && std::string(providerRestart.values().endpoint)=="https://example.test/v1");
    assert(!reboot.set(AiSettings::Field::Model,"bad model"));
    assert(!reboot.set(AiSettings::Field::Key,"key\r\nInjected: value"));
    assert(!reboot.set(AiSettings::Field::Key,std::string("a\0b",3)));
    assert(reboot.set(AiSettings::Field::Key,std::string(255,'z')));
    assert(!reboot.set(AiSettings::Field::Key,std::string(256,'z')));
    assert(reboot.set(AiSettings::Field::Key,"short"));
    for(size_t n=5;n<sizeof(reboot.values().key);++n)assert(!reboot.values().key[n]);
    AiSettings usbRestart;usbRestart.begin();assert(std::string(usbRestart.values().key)=="short");
    assert(reboot.set(AiSettings::Field::Key,""));assert(!reboot.values().key[0]);
    assert(reboot.set(AiSettings::Field::Key,"test-key-never-real"));
    value.key[0]='\r';assert(!reboot.save(value));value=reboot.values();
    fakeNvs::failWrites=true;value.key[0]=0;assert(!reboot.save(value) && reboot.values().key[0]);fakeNvs::failWrites=false;
    fakeNvs::blobs["deckai/config"][10]^=1;AiSettings corrupt;corrupt.begin();assert(!corrupt.values().key[0] && std::string(corrupt.values().endpoint)=="https://api.openai.com/v1");
    fakeNvs::blobs["deckai/config"].pop_back();AiSettings truncated;truncated.begin();assert(std::string(truncated.values().model)=="gpt-5.6-luna");
    std::mt19937 rng(0x41494348);for(unsigned n=0;n<6000;++n){std::string malformed;for(unsigned i=0,count=rng()%1024;i<count;++i)malformed+=char(rng()%256);(void)aiResponse(200,n&1?response('"'+malformed+'"'):malformed);}
    std::cout<<"PASS AI JSON/HTTPS settings, request escaping, UTF-8/surrogates, reply/error bounds, secret-safe errors, NVS isolation/rollback/corruption and 6000 malformed responses\n";
}
