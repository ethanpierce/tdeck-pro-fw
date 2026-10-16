#include "at_engine.h"
#include <cctype>
#include <utility>

namespace deck {
bool AtEngine::enqueue(AtCommand command, bool urgent) {
    if (queue_.size() >= 16 || command.text.empty() ||
        command.text.find_first_of("\r\n") != std::string::npos) return false;
    if (urgent) queue_.push_front(std::move(command));
    else queue_.push_back(std::move(command));
    return true;
}
void AtEngine::reset(const std::string& reason) {
    cooling_ = true;
    cooledAt_ = now_;
    line_.clear();
    dropping_ = false;
    prompted_ = false;
    if (active_) {
        finish(false, reason, true);
    } else {
        auto discarded = std::move(queue_);
        queue_.clear();
        for (auto& item : discarded)
            if (item.done) item.done({false, false, "Cancelled after modem reset", {}});
    }
}
void AtEngine::tick(uint32_t now) {
    now_ = now;
    if (active_ && uint32_t(now - started_) >= command_.timeoutMs) {
        // Abort SMS input and quarantine late results. Never retry a submitted SMS.
        tx_(std::string(1, '\x1b'));
        cooling_ = true;
        cooledAt_ = now;
        finish(false, "Timeout; result unknown. Refresh before retrying.", true);
    }
    if (cooling_) {
        if (uint32_t(now - cooledAt_) < 2000) return;
        cooling_ = false;
        line_.clear();
        dropping_ = false;
    }
    if (!active_ && !queue_.empty()) {
        command_ = std::move(queue_.front());
        queue_.pop_front();
        result_ = {};
        responseBytes_ = 0;
        prompted_ = false;
        active_ = true;
        started_ = now;
        tx_(command_.text + "\r");
    }
}
void AtEngine::feed(char byte) {
    if (byte == '\r') return;
    if (active_ && !command_.payload.empty() && !prompted_ && byte == '>' &&
        line_.find_first_not_of(' ') == std::string::npos) {
        line_.clear();
        prompted_ = true;
        started_ = now_;
        tx_(command_.payload + '\x1a');
        return;
    }
    if (byte == '\n') {
        if (!dropping_) line(line_);
        line_.clear();
        dropping_ = false;
        return;
    }
    if (dropping_) return;
    if (line_.size() >= 1024) {
        line_.clear();
        dropping_ = true;
        if (active_) {
            cooling_ = true;
            cooledAt_ = now_;
            finish(false, "Oversize modem response", true);
        }
    } else line_ += byte;
}
void AtEngine::line(const std::string& raw) {
    auto first = raw.find_first_not_of(" \t");
    if (first == std::string::npos) return;
    std::string value = raw.substr(first, raw.find_last_not_of(" \t") - first + 1);
    bool callEnd = value == "NO CARRIER" || value == "BUSY" || value == "NO ANSWER" ||
        value == "NO DIALTONE" || value == "NO DIAL TONE";
    bool registration = value.rfind("+CEREG:", 0) == 0 || value.rfind("+CREG:", 0) == 0 ||
        value.rfind("+CGREG:", 0) == 0;
    bool pin = value.rfind("+CPIN:", 0) == 0;
    bool unsolicited = callEnd || value == "RING" || value == "RDY" ||
        value.rfind("+CMTI:", 0) == 0 || value.rfind("+CLIP:", 0) == 0 ||
        value.rfind("+CRING:", 0) == 0 || value.rfind("VOICE CALL:", 0) == 0 ||
        value.rfind("VOICE CALL END:", 0) == 0 ||
        registration || pin || value == "SMS DONE" || value == "PB DONE";
    // Cancel old callbacks before the URC handler queues a fresh initialization.
    if (value == "RDY") reset("Modem restarted; previous result unknown");
    if (unsolicited) urc_(value);
    if (!active_ || cooling_) return;
    if (value == command_.text) return; // Echo before ATE0 takes effect.
    if (value == "OK") { finish(true, value); return; }
    if (value == "ERROR" || value.rfind("+CME ERROR", 0) == 0 ||
        value.rfind("+CMS ERROR", 0) == 0 || (callEnd && command_.callControl)) {
        finish(false, value); return;
    }
    if (unsolicited) {
        bool queryResponse = (pin && command_.text == "AT+CPIN?") ||
            (registration && command_.text == "AT" + value.substr(0, value.find(':')) + "?");
        if (!queryResponse) return;
    }
    if (command_.onLine) {
        // A callback may reset the engine, which destroys command_.onLine.
        auto callback = command_.onLine;
        callback(value);
        return;
    }
    responseBytes_ += value.size();
    if (result_.lines.size() >= 48 || responseBytes_ > 8192) {
        cooling_ = true;
        cooledAt_ = now_;
        finish(false, "Modem response limit exceeded", true);
        return;
    }
    result_.lines.push_back(value);
}
void AtEngine::finish(bool ok, const std::string& final, bool uncertain) {
    result_.ok = ok;
    result_.uncertain = uncertain;
    result_.final = final;
    active_ = false;
    auto callback = std::move(command_.done);
    AtResult completed = std::move(result_);
    // Flush pending work on desynchronization before callbacks enqueue new work.
    if (uncertain) {
        auto discarded = std::move(queue_);
        queue_.clear();
        for (auto& item : discarded)
            if (item.done) item.done({false, false, "Cancelled after modem desynchronization", {}});
    }
    if (callback) callback(completed);
}
std::vector<std::string> csv(const std::string& value) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (char c : value) {
        if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) { fields.push_back(field); field.clear(); }
        else if (quoted || !std::isspace(static_cast<unsigned char>(c))) field += c;
    }
    fields.push_back(field);
    return fields;
}
bool phoneNumber(const std::string& value) {
    size_t start = !value.empty() && value[0] == '+' ? 1 : 0;
    if (value.size() - start < 3 || value.size() - start > 15) return false;
    for (size_t i = start; i < value.size(); ++i)
        if (value[i] < '0' || value[i] > '9') return false;
    return true;
}
int registration(const std::string& line) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return -1;
    auto payload = line.substr(colon + 1);
    auto fields = csv(payload);
    auto number = [](const std::string& field) {
        if (field.empty() || field.size() > 2) return -1;
        int n = 0;
        for (char c : field) {
            if (c < '0' || c > '9') return -1;
            n = n * 10 + c - '0';
        }
        return n <= 11 ? n : -1;
    };
    // Query: <n>,<stat>[,"tac",...]. URC: <stat>[,"tac",...].
    // Keep quoting information: a quoted numeric TAC is still a URC location.
    auto comma = payload.find(',');
    auto next = comma == std::string::npos ? std::string::npos : payload.find_first_not_of(" \t", comma + 1);
    if (comma != std::string::npos && next == std::string::npos) return -1;
    if (fields.size() >= 2 && next != std::string::npos && payload[next] != '"') {
        int mode = number(fields[0]);
        if (mode < 0 || mode > 5) return -1;
        return number(fields[1]);
    }
    return number(fields[0]);
}
}
