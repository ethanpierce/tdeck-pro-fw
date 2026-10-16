#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deck {
constexpr size_t ConversationMessageLimit=16,ConversationPreviewLimit=80;
enum class ConversationKind : uint8_t { Sms, NearbyLoRa };
// Views borrow the existing message-store records; no Arduino/NVS dependency.
struct ConversationMessage {
    bool radio=false;
    uint32_t sequence=0;
    std::string_view peer,text;
};
struct Conversation {
    ConversationKind kind=ConversationKind::Sms;
    std::string key,replyAddress,preview;
    uint32_t latestSequence=0;
    size_t messageCount=0;
};
// Numeric peers only: optional leading '+' is ignored for matching, while the
// original latest address remains in Conversation::replyAddress for replies.
std::string conversationPeerKey(std::string_view peer);
bool conversationMatches(const Conversation& conversation,const ConversationMessage& message);
// Index at most the newest 16 valid records, newest conversation first. All
// LoRa broadcasts share one NearbyLoRa group; there is no radio DM/unread state.
// Previews use at most 80 UTF-8 bytes and flatten control characters for one row.
std::vector<Conversation> conversationIndex(const std::vector<ConversationMessage>& messages);
}
