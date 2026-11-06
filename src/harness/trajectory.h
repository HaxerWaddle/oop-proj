#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace agent::harness {

/**
 * @brief Lưu trữ dữ liệu của một bước thực thi trong quá trình chạy Agent (Mục 4.1 & 7.1)
 */
struct Step {
    int step_id{0};
    std::string thought;
    std::string action_type; // "tool_call", "click", "type_text", "key_press", "done"
    std::string tool_name;
    std::string tool_args;
    std::string tool_result;
    int tokens_used{0};
    long latency_ms{0};
};

/**
 * @brief Lưu trữ toàn bộ lịch sử (Trajectory) và thông số đo lường của 1 lần chạy Task
 */
class Trajectory {
public:
    std::string task_id;
    std::string model;
    bool success{false};
    int total_tokens{0};
    long total_time_ms{0};
    std::string final_answer;
    std::optional<double> eval_score{std::nullopt};
    std::string eval_reason;
    std::vector<Step> steps;

    Trajectory() = default;
    Trajectory(std::string id, std::string model_name);

    /**
     * @brief Ghi nhận thêm một bước thực thi vào Trajectory
     */
    void addStep(Step step);

    /**
     * @brief Chuyển đổi toàn bộ Trajectory sang chuỗi JSON định dạng chuẩn (Mục 7.1)
     */
    [[nodiscard]] std::string toJson(int indent = 2) const;

    /**
     * @brief Lưu Trajectory ra file JSON trên đĩa
     */
    [[nodiscard]] bool saveToFile(const std::filesystem::path& filepath) const;
};

} // namespace agent::harness