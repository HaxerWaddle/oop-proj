#pragma once

#include "tools/tool.h"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace agent::tools {

/**
 * @brief [KỸ THUẬT C++20]: Concept kiểm tra một kiểu T phải kế thừa từ Tool
 */
template <typename T>
concept DerivedFromTool = std::derived_from<T, Tool> && !std::is_abstract_v<T>;

/**
 * @brief Quản lý vòng đời, chính sách truy cập (Policy) và khởi tạo động các Tool.
 * Áp dụng Design Pattern: Registry / Factory Pattern.
 */
class ToolRegistry {
public:
    using ToolFactory = std::function<std::unique_ptr<Tool>()>;

    ToolRegistry() = default;
    ~ToolRegistry() = default;

    /**
     * @brief Đăng ký một instance Tool trực tiếp tại runtime
     */
    void registerTool(std::shared_ptr<Tool> tool);

    /**
     * @brief Đăng ký một Factory tạo Tool theo tên (Lazy instantiation)
     */
    void registerFactory(const std::string& name, ToolFactory factory);

    /**
     * @brief [KỸ THUẬT C++20 Concept + C++17 Template]: Đăng ký nhanh class kế thừa từ Tool
     */
    template <DerivedFromTool T, typename... Args>
    void registerToolClass(Args&&... args) {
        auto instance = std::make_shared<T>(std::forward<Args>(args)...);
        const std::string tool_name = instance->name();
        tools_[tool_name] = std::move(instance);
    }

    /**
     * @brief Đăng ký nhanh một Tool bằng Lambda tại runtime
     */
    void registerLambdaTool(
        const std::string& name,
        const std::string& description,
        CallbackTool::ExecuteCallback callback);

    /**
     * @brief Đăng ký toàn bộ 5 tool chuẩn + 3 tool mở rộng (OpenClaw/Hermes style)
     */
    void registerBuiltinTools(const std::string& sqlite_db_path = "agent_memory.db");

    /**
     * @brief Truy xuất Tool theo tên (nếu chưa có instance nhưng có Factory thì tự khởi tạo)
     */
    [[nodiscard]] std::shared_ptr<Tool> getTool(const std::string& name);

    /**
     * @brief Thiết lập Tool Policy: danh sách cho phép (allow_list) và từ chối (deny_list)
     * Nếu allow_list không rỗng, chỉ các tool nằm trong allow_list mới được chạy.
     * Bất kỳ tool nào nằm trong deny_list sẽ luôn bị chặn.
     */
    void setPolicy(const std::vector<std::string>& allow_list,
                   const std::vector<std::string>& deny_list);

    /**
     * @brief Kiểm tra xem một tool có được phép thực thi theo Policy hiện hành hay không
     */
    [[nodiscard]] bool isAllowed(const std::string& name) const;

    /**
     * @brief Kiểm tra Policy và thực thi Tool theo tên
     */
    [[nodiscard]] std::optional<ToolResult> executeTool(
        const std::string& name,
        const std::string& args_json);

    /**
     * @brief Sinh đoạn mô tả danh sách các Tool được phép dùng để gắn vào System Prompt
     */
    [[nodiscard]] std::string generateToolsPrompt() const;

    /**
     * @brief Lấy danh sách tên tất cả các Tool đã đăng ký
     */
    [[nodiscard]] std::vector<std::string> listRegisteredTools() const;

private:
    std::unordered_map<std::string, std::shared_ptr<Tool>> tools_;
    std::unordered_map<std::string, ToolFactory> factories_;
    std::unordered_set<std::string> allowed_tools_;
    std::unordered_set<std::string> denied_tools_;
};

} // namespace agent::tools