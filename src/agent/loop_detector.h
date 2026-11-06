#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace agent::core {

/**
 * @brief Phân loại trạng thái lặp của Agent
 */
enum class LoopStatus {
    None,
    WarningRepeat,
    WarningPingPong,
    CriticalRepeat,
    CriticalPingPong
};

/**
 * @brief Kết quả kiểm tra lặp sau mỗi bước hành động
 */
struct LoopCheckResult {
    LoopStatus status{LoopStatus::None};
    std::string message;
};

/**
 * @brief Bộ phát hiện vòng lặp (Generic Repeat & Ping-Pong) với ngưỡng cấu hình được
 */
class LoopDetector {
public:
    explicit LoopDetector(size_t warning_threshold = 2, size_t critical_threshold = 3);
    ~LoopDetector() = default;

    /**
     * @brief Ghi nhận hành động mới và kiểm tra xem Agent có bị kẹt trong vòng lặp không
     */
    [[nodiscard]] LoopCheckResult recordAction(const std::string& tool_name,
                                               const std::string& tool_args);

    /**
     * @brief Xóa lịch sử hành động trước khi chạy task mới
     */
    void reset() noexcept;

    void setThresholds(size_t warning_threshold, size_t critical_threshold) noexcept;

private:
    size_t warning_threshold_;
    size_t critical_threshold_;
    std::vector<std::string> action_signatures_;

    [[nodiscard]] size_t countConsecutiveRepeats() const noexcept;
    [[nodiscard]] size_t countPingPongCycles() const noexcept;
};

} // namespace agent::core