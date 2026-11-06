#pragma once

#include "agent/loop_detector.h"
#include "agent/skill_loader.h"
#include "client/llm_client.h"
#include "tools/tool_registry.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace agent::core {

// ==============================================================================
// ĐỊNH NGHĨA CÁC LOẠI ACTION VỚI std::variant (KỸ THUẬT C++17 BẮT BUỘC - MỤC V)
// ==============================================================================

struct ToolCallAction {
    std::string tool_name;
    std::string args;
};

struct ClickAction {
    int x{0};
    int y{0};
};

struct TypeTextAction {
    std::string text;
};

struct KeyPressAction {
    std::string key;
};

struct DoneAction {
    std::string final_answer;
};

/**
 * @brief [KỸ THUẬT C++17]: std::variant đại diện cho các hành động trong AgentLoop
 * Bao gồm ToolCall thông thường, các thao tác GUI (Click, TypeText, KeyPress) và Done.
 */
using AgentAction = std::variant<
    ToolCallAction,
    ClickAction,
    TypeTextAction,
    KeyPressAction,
    DoneAction>;

/**
 * @brief Kết quả phân tích (parse) phản hồi của LLM ở mỗi bước
 */
struct ParsedResponse {
    std::string thought;
    AgentAction action;
};

/**
 * @brief Dữ liệu một bước thực thi được gửi qua Hook (Observer Pattern).
 * AgentLoop hoàn toàn KHÔNG biết HarnessRunner tồn tại, chỉ phát StepEvent qua callback.
 */
struct StepEvent {
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
 * @brief Kết quả tổng thể sau khi AgentLoop kết thúc một Task
 */
struct AgentRunResult {
    bool completed{false};
    bool stopped_by_loop{false};
    bool reached_max_steps{false};
    std::string final_answer;
    int total_tokens{0};
    long total_time_ms{0};
};

/**
 * @brief Lớp thực thi vòng lặp ReAct (Observe -> Think -> Act -> Observe).
 * Áp dụng Template Method Pattern cho run() và Observer/Hook Pattern cho step_hook_.
 */
class AgentLoop {
public:
    using StepHook = std::function<void(const StepEvent&)>;

    AgentLoop(std::shared_ptr<client::LLMClient> llm_client,
              std::shared_ptr<tools::ToolRegistry> tool_registry,
              SkillLoader skill_loader = SkillLoader("skills"),
              int max_steps = 10);

    virtual ~AgentLoop() = default;

    // [KỸ THUẬT C++26]: = delete("reason") cấm sao chép đối tượng điều phối AgentLoop
#if defined(__cpp_deleted_function) && __cpp_deleted_function >= 202403L
    AgentLoop(const AgentLoop&) = delete("AgentLoop holds stateful conversation and hooks; copying is forbidden.");
    AgentLoop& operator=(const AgentLoop&) = delete("AgentLoop cannot be copied.");
#else
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;
#endif

    /**
     * @brief [OBSERVER / HOOK PATTERN]: Cho phép tầng ngoài (như Harness) đăng ký hàm lắng nghe
     * từng bước chạy mà không làm AgentLoop phụ thuộc ngược vào tầng đó.
     */
    void setStepHook(StepHook hook) { step_hook_ = std::move(hook); }

    void setMaxSteps(int max_steps) noexcept { max_steps_ = max_steps; }
    [[nodiscard]] int getMaxSteps() const noexcept { return max_steps_; }

    [[nodiscard]] LoopDetector& getLoopDetector() noexcept { return loop_detector_; }
    [[nodiscard]] SkillLoader& getSkillLoader() noexcept { return skill_loader_; }

    /**
     * @brief [TEMPLATE METHOD PATTERN]: Định nghĩa khung xương (skeleton) của vòng lặp ReAct.
     * Gọi tuần tự các bước ảo: observe() -> think() -> parseLLMOutput() -> act().
     */
    [[nodiscard]] AgentRunResult run(
        const std::string& task_instruction,
        const std::vector<std::string>& initial_images_base64 = {});

protected:
    /**
     * @brief Bước 1 (Observe): Chuyển kết quả của Tool ở bước trước thành thông điệp quan sát
     */
    [[nodiscard]] virtual std::string observe(
        const tools::ToolResult& last_tool_result,
        int step_id);

    /**
     * @brief Bước 2 (Think): Gửi lịch sử hội thoại tới LLM để suy luận bước tiếp theo
     */
    [[nodiscard]] virtual std::expected<client::LLMResponse, client::LLMError> think(
        const std::vector<client::Message>& conversation_history);

    /**
     * @brief Bước 3 (Act): Thực thi hành động bằng std::visit + if constexpr trên AgentAction
     */
    [[nodiscard]] virtual std::optional<tools::ToolResult> act(const AgentAction& action);

    /**
     * @brief Xây dựng System Prompt kết hợp hướng dẫn format ReAct JSON, danh sách Tool và Skill
     */
    [[nodiscard]] virtual std::string buildSystemPrompt(const std::string& task_instruction) const;

    /**
     * @brief Phân tích phản hồi từ LLM (hỗ trợ cả JSON format và Regex ReAct format)
     */
    [[nodiscard]] static ParsedResponse parseLLMOutput(const std::string& raw_content);

    std::shared_ptr<client::LLMClient> client_;
    std::shared_ptr<tools::ToolRegistry> tool_registry_;
    SkillLoader skill_loader_;
    LoopDetector loop_detector_;
    int max_steps_{10};
    StepHook step_hook_;
};

} // namespace agent::core