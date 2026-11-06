#pragma once

#include <functional>
#include <optional>
#include <string>

namespace agent::tools {

/**
 * @brief Kết quả trả về sau khi thực thi một Tool
 */
struct ToolResult {
    bool success{false};
    std::string output;
    std::optional<std::string> error_message{std::nullopt};
};

/**
 * @brief Abstract Interface cho tất cả các Tool trong hệ thống.
 * Đảm bảo nguyên tắc: Tool hoàn toàn độc lập, không phụ thuộc vào AgentLoop.
 */
class Tool {
public:
    Tool() = default;
    virtual ~Tool() = default;

    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;
    Tool(Tool&&) noexcept = default;
    Tool& operator=(Tool&&) noexcept = default;

    /**
     * @brief Tên định danh của tool (ví dụ: "exec", "file", "calculator")
     */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief Mô tả chức năng và định dạng tham số JSON để LLM đọc hiểu
     */
    [[nodiscard]] virtual std::string description() const = 0;

    /**
     * @brief Thực thi tool với chuỗi tham số đầu vào (JSON hoặc raw string)
     * [KỸ THUẬT C++17]: Sử dụng std::optional<ToolResult> cho kết quả có thể rỗng
     */
    [[nodiscard]] virtual std::optional<ToolResult> execute(const std::string& args_json) = 0;
};

/**
 * @brief Lớp Tool tổng quát sử dụng std::function + Lambda (Kỹ thuật C++17)
 * Cho phép đăng ký nhanh các công cụ mới tại runtime mà không cần tạo class rời.
 */
class CallbackTool final : public Tool {
public:
    using ExecuteCallback = std::function<std::optional<ToolResult>(const std::string&)>;

    CallbackTool(std::string tool_name, std::string tool_desc, ExecuteCallback callback)
        : name_(std::move(tool_name)),
          description_(std::move(tool_desc)),
          callback_(std::move(callback)) {}

    [[nodiscard]] std::string name() const override { return name_; }
    [[nodiscard]] std::string description() const override { return description_; }

    [[nodiscard]] std::optional<ToolResult> execute(const std::string& args_json) override {
        if (!callback_) {
            return std::nullopt;
        }
        return callback_(args_json);
    }

private:
    std::string name_;
    std::string description_;
    ExecuteCallback callback_;
};

// ==============================================================================
// KHAI BÁO 5 CONCRETE TOOLS CỐT LÕI (Triển khai trong các file .cpp tương ứng)
// ==============================================================================

/**
 * @brief 1. ExecTool: Chạy lệnh shell và trả về stdout + stderr
 */
class ExecTool final : public Tool {
public:
    [[nodiscard]] std::string name() const override { return "exec"; }
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] std::optional<ToolResult> execute(const std::string& args_json) override;
};

/**
 * @brief 2. FileTool: Đọc và ghi file hệ thống (read_file / write_file)
 */
class FileTool final : public Tool {
public:
    [[nodiscard]] std::string name() const override { return "file"; }
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] std::optional<ToolResult> execute(const std::string& args_json) override;
};

/**
 * @brief 3. WebSearchTool: Tìm kiếm thông tin qua DuckDuckGo / SearXNG API
 */
class WebSearchTool final : public Tool {
public:
    explicit WebSearchTool(std::string searxng_base_url = "");
    [[nodiscard]] std::string name() const override { return "web_search"; }
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] std::optional<ToolResult> execute(const std::string& args_json) override;

private:
    std::string searxng_base_url_;
};

/**
 * @brief 4. MemoryTool: Lưu và truy vấn bộ nhớ qua SQLite (memory_save / memory_search)
 */
class MemoryTool final : public Tool {
public:
    explicit MemoryTool(std::string db_path = "agent_memory.db");
    [[nodiscard]] std::string name() const override { return "memory"; }
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] std::optional<ToolResult> execute(const std::string& args_json) override;

private:
    std::string db_path_;
    bool ensureTableInitialized() const;
};

/**
 * @brief 5. CalculatorTool: Tính giá trị biểu thức số học
 */
class CalculatorTool final : public Tool {
public:
    [[nodiscard]] std::string name() const override { return "calculator"; }
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] std::optional<ToolResult> execute(const std::string& args_json) override;
};

} // namespace agent::tools