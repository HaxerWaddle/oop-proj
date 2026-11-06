#include "agent/loop_detector.h"

namespace agent::core {

LoopDetector::LoopDetector(size_t warning_threshold, size_t critical_threshold)
    : warning_threshold_(warning_threshold),
      critical_threshold_(critical_threshold) {}

void LoopDetector::reset() noexcept {
    action_signatures_.clear();
}

void LoopDetector::setThresholds(size_t warning_threshold, size_t critical_threshold) noexcept {
    warning_threshold_ = warning_threshold;
    critical_threshold_ = critical_threshold;
}

size_t LoopDetector::countConsecutiveRepeats() const noexcept {
    if (action_signatures_.empty()) {
        return 0;
    }

    const std::string& latest = action_signatures_.back();
    size_t count = 0;
    for (auto it = action_signatures_.rbegin(); it != action_signatures_.rend(); ++it) {
        if (*it == latest) {
            ++count;
        } else {
            break;
        }
    }
    return count;
}

size_t LoopDetector::countPingPongCycles() const noexcept {
    const size_t n = action_signatures_.size();
    if (n < 4) {
        return 0;
    }

    const std::string& b = action_signatures_[n - 1];
    const std::string& a = action_signatures_[n - 2];
    if (a == b) {
        return 0; // Đây là Generic Repeat, không phải Ping-Pong (A != B)
    }

    size_t pairs = 0;
    size_t idx = n;
    while (idx >= 2) {
        if (action_signatures_[idx - 1] == b && action_signatures_[idx - 2] == a) {
            ++pairs;
            idx -= 2;
        } else {
            break;
        }
    }
    return pairs;
}

LoopCheckResult LoopDetector::recordAction(const std::string& tool_name,
                                           const std::string& tool_args) {
    action_signatures_.push_back(tool_name + "::" + tool_args);

    // 1. Kiểm tra Generic Repeat (A -> A -> A)
    const size_t repeat_count = countConsecutiveRepeats();
    if (repeat_count >= critical_threshold_) {
        return LoopCheckResult{
            LoopStatus::CriticalRepeat,
            "[CRITICAL LOOP] Generic repeat detected: tool '" + tool_name +
                "' called identically " + std::to_string(repeat_count) + " times."
        };
    }
    if (repeat_count >= warning_threshold_) {
        return LoopCheckResult{
            LoopStatus::WarningRepeat,
            "[WARNING LOOP] Tool '" + tool_name + "' repeated " +
                std::to_string(repeat_count) + " times with identical arguments."
        };
    }

    // 2. Kiểm tra Ping-Pong Loop (A -> B -> A -> B)
    const size_t ping_pong_cycles = countPingPongCycles();
    if (ping_pong_cycles >= critical_threshold_) {
        return LoopCheckResult{
            LoopStatus::CriticalPingPong,
            "[CRITICAL LOOP] Ping-pong alternation detected across " +
                std::to_string(ping_pong_cycles) + " cycles."
        };
    }
    if (ping_pong_cycles >= warning_threshold_) {
        return LoopCheckResult{
            LoopStatus::WarningPingPong,
            "[WARNING LOOP] Potential ping-pong pattern detected (" +
                std::to_string(ping_pong_cycles) + " alternating cycles)."
        };
    }

    return LoopCheckResult{LoopStatus::None, ""};
}

} // namespace agent::core