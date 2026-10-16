#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace deck {
constexpr size_t AiPromptLimit=384, AiReplyLimit=2048, AiBodyLimit=8192, AiHistoryLimit=8;
struct AiConfig {
    uint32_t magic=0x444b4101;
    char endpoint[192]="https://api.openai.com/v1";
    char model[96]="gpt-5.6-luna";
    char key[256]={};
    uint32_t checksum=0;
};
struct AiMessage {bool assistant=false;std::string text;};
bool validAiEndpoint(const std::string& value);
bool validAiConfig(const AiConfig& value);
uint32_t aiConfigChecksum(const AiConfig& value);
std::string aiCompletionUrl(const std::string& endpoint);
bool aiRequest(const std::string& model,const std::vector<AiMessage>& history,const std::string& prompt,std::string& json,bool openAiLunaProfile=false);
struct AiReply {bool ok=false;bool truncated=false;std::string text,error;};
AiReply aiResponse(int status,const std::string& body);
std::vector<std::string> aiDisplayLines(const std::vector<AiMessage>& history,size_t columns=35);
size_t aiLastPageOffset(const std::vector<std::string>& lines,size_t pageSize=16);
}
