#pragma once

#include "client/llm_client.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace agent::harness {

/**
 * @brief Cấu trúc đặc tả một Task được đọc từ file benchmark/tasks.json (Mục 7.2)
 */
struct TaskSpec {
    std::string id;
    std::string description;
    std::string instruction;
    std::string eval_type{"functional"}; // "functional", "keyword", hoặc "vlm"
    std::string eval_script;             // Lệnh shell kiểm tra cho FunctionalEvaluator
    std::vector<std::string> expected_keywords; // Danh sách từ khóa cho KeywordEvaluator
    int max_steps{10};
};

/**
 * @brief Kết quả đánh giá của Evaluator.
 * [KỸ THUẬT C++17 - MỤC V]: Dùng std::optional<double> cho partial_score.
 */
struct EvalResult {
    bool passed{false};
    std::optional<double> partial_score{std::nullopt};
    std::string reason;
};

/**
 * @brief Abstract Interface cho các chiến lược chấm điểm (Strategy Pattern).
 * Tuân thủ nguyên tắc thiết kế Mục 4.4: Evaluator KHÔNG phụ thuộc vào cách Agent thực thi,
 * chỉ nhìn vào yêu cầu của Task và kết quả đầu ra cuối cùng.
 */
class Evaluator {
public:
    Evaluator() = default;
    virtual ~Evaluator() = default;

    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;
    Evaluator(Evaluator&&) noexcept = default;
    Evaluator& operator=(Evaluator&&) noexcept = default;

    /**
     * @brief Phương thức thuần ảo đánh giá kết quả thực hiện Task
     */
    [[nodiscard]] virtual EvalResult evaluate(
        const TaskSpec& task,
        const std::string& final_answer) const = 0;
};

/**
 * @brief Chiến lược 1: Đánh giá dựa trên sự xuất hiện của các từ khóa kỳ vọng
 */
class KeywordEvaluator final : public Evaluator {
public:
    KeywordEvaluator() = default;
    explicit KeywordEvaluator(std::vector<std::string> default_keywords);

    [[nodiscard]] EvalResult evaluate(
        const TaskSpec& task,
        const std::string& final_answer) const override;

private:
    std::vector<std::string> default_keywords_;
    [[nodiscard]] static std::string toLower(std::string str);
};

/**
 * @brief Chiến lược 2: Đánh giá chức năng bằng cách chạy eval_script trên hệ thống
 * Ví dụ: "test -f result.txt && grep 255 result.txt && echo PASS"
 */
class FunctionalEvaluator final : public Evaluator {
public:
    FunctionalEvaluator() = default;

    [[nodiscard]] EvalResult evaluate(
        const TaskSpec& task,
        const std::string& final_answer) const override;
};

/**
 * @brief Chiến lược 3: Đánh giá ngữ nghĩa / thị giác thông qua mô hình LLM/VLM (Mục 4.1)
 */
class VLMEvaluator final : public Evaluator {
public:
    explicit VLMEvaluator(std::shared_ptr<client::LLMClient> vlm_client);

    [[nodiscard]] EvalResult evaluate(
        const TaskSpec& task,
        const std::string& final_answer) const override;

private:
    std::shared_ptr<client::LLMClient> vlm_client_;
};

} // namespace agent::harness