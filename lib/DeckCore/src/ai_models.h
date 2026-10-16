#pragma once
#include <cstddef>
#include <string>
namespace deck {
struct AiModelPreset { const char* label; const char* id; };
// Verified against OpenAI's model documentation on 2026-10-16. Custom IDs remain
// available because a preset does not establish an account's model access.
inline constexpr AiModelPreset AiModelPresets[]={
    {"GPT-5.6 Luna","gpt-5.6-luna"},
    {"GPT-5.6 Terra","gpt-5.6-terra"},
    {"GPT-5.6 Sol","gpt-5.6-sol"},
    {"GPT-5.4 Mini","gpt-5.4-mini"},
};
inline constexpr size_t AiModelPresetCount=sizeof(AiModelPresets)/sizeof(AiModelPresets[0]);
inline bool boundedOpenAiModel(const std::string& model) {
    for(const auto& preset:AiModelPresets)if(model==preset.id)return true;
    return false;
}
}
