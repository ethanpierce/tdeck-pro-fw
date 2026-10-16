#include <conversations.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>

using namespace deck;
int main() {
    assert(conversationPeerKey("+15551234567")=="15551234567");
    assert(conversationPeerKey("15551234567")=="15551234567");
    assert(conversationPeerKey("00123")=="00123");
    for(const auto bad:{"","+","12","+12","++123","123abc"," 123","12 3","123-456","1234567890123456"})
        assert(conversationPeerKey(bad).empty());
    assert(conversationIndex({}).empty());
    std::vector<ConversationMessage> messages={
        {false,1,"15551234567","outgoing"},
        {true,6,"node-a","nearby latest"},
        {false,9,"+15551234567","newest reply"},
        {true,2,"node-b","another broadcaster"},
        {false,7,"+15559876543","second contact"},
        {false,11,"invalid peer","not a numeric SMS conversation"}
    };
    auto threads=conversationIndex(messages);
    assert(threads.size()==3 && threads[0].latestSequence==9 && threads[1].latestSequence==7 && threads[2].latestSequence==6);
    assert(threads[0].key=="15551234567" && threads[0].replyAddress=="+15551234567" && threads[0].messageCount==2 && threads[0].preview=="newest reply");
    assert(conversationMatches(threads[0],messages[0]) && conversationMatches(threads[0],messages[2]));
    assert(!conversationMatches(threads[0],messages[1]) && !conversationMatches(threads[0],messages[4]));
    assert(threads[2].kind==ConversationKind::NearbyLoRa && threads[2].replyAddress.empty() && threads[2].messageCount==2);
    assert(conversationMatches(threads[2],messages[1]) && conversationMatches(threads[2],messages[3]));
    assert(!conversationMatches(threads[2],messages[0]));
    // UI names can use the original reply address; no contact names or unread state are invented.
    messages.push_back({false,10,"15551234567","latest original address"});
    assert(conversationIndex(messages)[0].replyAddress=="15551234567");
    const std::string boundary=std::string(79,'x')+"\xc3\xa9";
    auto clipped=conversationIndex({{false,1,"123",boundary}});
    assert(clipped[0].preview==std::string(79,'x'));
    clipped=conversationIndex({{false,1,"123","line1\nline2\t\r"}});
    assert(clipped[0].preview=="line1 line2  ");
    const std::string malformed="\xed\xa0\x80\xf4\x90\x80\x80\xc0\xaf";
    clipped=conversationIndex({{false,1,"123",malformed}});
    assert(clipped[0].preview==std::string(malformed.size(),'?'));
    std::vector<std::string> peers;
    peers.reserve(40);
    for(unsigned i=0;i<40;++i) peers.push_back(std::to_string(1000+i));
    messages.clear();
    for(unsigned i=0;i<40;++i) messages.push_back({false,i+1,peers[i],"bounded"});
    std::reverse(messages.begin(),messages.end());
    threads=conversationIndex(messages);
    assert(threads.size()==16 && threads.front().latestSequence==40 && threads.back().latestSequence==25);
    messages={{false,3,"+123","first"},{false,3,"123","last equal sequence"}};
    assert(conversationIndex(messages)[0].preview=="last equal sequence");
    std::mt19937 random(0x434f4e56);
    for(unsigned i=0;i<6000;++i) {
        std::string body;
        for(unsigned n=random()%200;n;--n)body+=char(random()%256);
        const auto rows=conversationIndex({{false,uint32_t(random()),"+15551234567",body},{true,uint32_t(random()),"",body}});
        assert(rows.size()==2);
        for(const auto& row:rows)assert(row.preview.size()<=ConversationPreviewLimit && row.messageCount==1);
    }
    std::cout<<"PASS conversation grouping, numeric normalization/reply preservation, broadcast scope, sequence ordering, 16-record/UTF-8 preview bounds and 6000 malformed previews\n";
}
