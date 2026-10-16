#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace deck {
struct AtResult {
    bool ok = false;
    bool uncertain = false;
    std::string final;
    std::vector<std::string> lines;
};
struct AtCommand {
    std::string text;
    uint32_t timeoutMs = 5000;
    std::string payload; // Nonempty: wait for '>', then send this and Ctrl-Z.
    bool callControl = false;
    std::function<void(const AtResult&)> done;
    std::function<void(const std::string&)> onLine = {}; // Stream large inbox scans without buffering them.
};
// Single UART owner. Bounded input, explicit command results, interleaved URCs.
class AtEngine {
public:
    using Sink = std::function<void(const std::string&)>;
    AtEngine(Sink tx, Sink urc) : tx_(tx), urc_(urc) {}
    bool enqueue(AtCommand command, bool urgent = false);
    // A reboot/power cycle invalidates the current exchange. Never retry it automatically.
    void reset(const std::string& reason);
    void feed(char byte);
    void tick(uint32_t now);
    bool idle() const { return !active_ && queue_.empty() && !cooling_; }
private:
    void line(const std::string& value);
    void finish(bool ok, const std::string& final, bool uncertain = false);
    Sink tx_, urc_;
    std::deque<AtCommand> queue_;
    AtCommand command_;
    AtResult result_;
    std::string line_;
    uint32_t now_ = 0, started_ = 0, cooledAt_ = 0;
    size_t responseBytes_ = 0;
    bool active_ = false, prompted_ = false, dropping_ = false, cooling_ = false;
};
std::vector<std::string> csv(const std::string& value);
bool phoneNumber(const std::string& value);
int registration(const std::string& line);
}
