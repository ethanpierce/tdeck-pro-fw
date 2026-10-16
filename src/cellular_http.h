#pragma once
#include <ai_protocol.h>
// One authenticated HTTPS POST over the modem's TCP service. Never retries or
// follows redirects, and never uses the modem's unverifiable TLS defaults.
deck::AiReply cellularAiRequest(const std::string& url,const std::string& key,const std::string& body);
