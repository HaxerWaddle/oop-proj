#include "tools/tool_registry.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace agent::tools {

void ToolRegistry::registerTool(std::shared_ptr<Tool> tool) {
    if (!tool) {
        return;
    }
    tools_[tool->name()] = std::move(tool);
}

void ToolRegistry::registerFactory(const std::string& name, ToolFactory factory) {
    if (name.empty() || !factory) {
        return;
    }
    factories_[name] = std::move(factory);
}

void ToolRegistry::registerLambdaTool(
    const std::string& name,
    const std::string& description,
    CallbackTool::ExecuteCallback callback) {
    auto tool = std::make_shared<CallbackTool>(name, description, std::move(callback));
    registerTool(std::move(tool));
}

std::shared_ptr<Tool> ToolRegistry::getTool(const std::string& name) {
    if (auto it = tools_.find(name); it != tools_.end()) {
        return it->second;
    }

    // Nếu được đăng ký qua Factory, khởi tạo lazy và lưu vào map
    if (auto fit = factories_.find(name); fit != factories_.end()) {
        std::unique_ptr<Tool> created = fit->second();
        if (created) {
            std::shared_ptr<Tool> shared_tool = std::move(created);
            tools_[name] = shared_tool;
            return shared_tool;
        }
    }

    return nullptr;
}

void ToolRegistry::setPolicy(
    const std::vector<std::string>& allow_list,
    const std::vector<std::string>& deny_list) {
    allowed_tools_.clear();
    denied_tools_.clear();

    for (const auto& name : allow_list) {
        allowed_tools_.insert(name);
    }
    for (const auto& name : deny_list) {
        denied_tools_.insert(name);
    }
}

bool ToolRegistry::isAllowed(const std::string& name) const {
    if (denied_tools_.contains(name)) {
        return false;
    }
    if (!allowed_tools_.empty() && !allowed_tools_.contains(name)) {
        return false;
    }
    return true;
}

std::optional<ToolResult> ToolRegistry::executeTool(
    const std::string& name,
    const std::string& args_json) {
    if (!isAllowed(name)) {
        return ToolResult{
            false,
            "",
            "Tool '" + name + "' is blocked by current ToolRegistry security policy."
        };
    }

    auto tool = getTool(name);
    if (!tool) {
        return ToolResult{
            false,
            "",
            "Tool '" + name + "' is not registered in ToolRegistry."
        };
    }

    return tool->execute(args_json);
}

std::string ToolRegistry::generateToolsPrompt() const {
    std::ostringstream oss;
    oss << "Available Tools:\n";

    // [KỸ THUẬT C++17]: Range-based for + Structured Bindings
    for (const auto& [name, tool_ptr] : tools_) {
        if (tool_ptr && isAllowed(name)) {
            oss << "- Tool Name: \"" << name << "\"\n"
                << "  Description: " << tool_ptr->description() << "\n";
        }
    }
    return oss.str();
}

std::vector<std::string> ToolRegistry::listRegisteredTools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    return names;
}

void ToolRegistry::registerBuiltinTools(const std::string& sqlite_db_path) {
    // 1. Đăng ký 5 Concrete Tools bắt buộc
    registerToolClass<ExecTool>();
    registerToolClass<FileTool>();
    registerToolClass<WebSearchTool>();
    registerToolClass<MemoryTool>(sqlite_db_path);
    registerToolClass<CalculatorTool>();

    // 2. Đăng ký thêm 3 Tool thuộc 3 loại khác nhau (Tham khảo OpenClaw & Hermes)
    // bằng std::function + Lambda tại runtime:

    // Loại 1 (Temporal/Clock Utility - Hermes style): Lấy thời gian thực hệ thống
    registerLambdaTool(
        "datetime_info",
        "Get current local date, time, and Unix timestamp. Args: {} (no arguments required).",
        [](const std::string& /*args_json*/) -> std::optional<ToolResult> {
            const auto now = std::chrono::system_clock::now();
            const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S %Z");
            return ToolResult{true, oss.str(), std::nullopt};
        });

    // Loại 2 (Environment/Workspace Inspector - OpenClaw style): Kiểm tra thư mục làm việc & biến môi trường
    registerLambdaTool(
        "system_inspect",
        "Inspect current working directory and environment variables. Args JSON: {\"env\": \"PATH\"} or {}.",
        [](const std::string& args_json) -> std::optional<ToolResult> {
            nlohmann::json result_json;
            std::error_code ec;
            result_json["cwd"] = std::filesystem::current_path(ec).string();

            try {
                if (!args_json.empty() && args_json.front() == '{') {
                    auto parsed = nlohmann::json::parse(args_json);
                    if (parsed.contains("env") && parsed["env"].is_string()) {
                        const std::string env_name = parsed["env"].get<std::string>();
                        const char* val = std::getenv(env_name.c_str());
                        result_json["env_value"] = val ? std::string(val) : "";
                    }
                }
            } catch (...) {
                // Bỏ qua nếu args_json không phải JSON
            }
            return ToolResult{true, result_json.dump(), std::nullopt};
        });

    // Loại 3 (Structured Data Processor - Hermes JSON extractor): Trích xuất trường từ chuỗi JSON
    registerLambdaTool(
        "json_query",
        "Extract a field from a JSON object. Args JSON: {\"json_str\": \"{...}\", \"key\": \"field_name\"}.",
        [](const std::string& args_json) -> std::optional<ToolResult> {
            try {
                const auto parsed = nlohmann::json::parse(args_json);
                const std::string raw = parsed.value("json_str", "");
                const std::string key = parsed.value("key", "");
                const auto target = nlohmann::json::parse(raw);
                if (!target.contains(key)) {
                    return ToolResult{false, "", "Key '" + key + "' not found in JSON."};
                }
                const auto& val = target[key];
                return ToolResult{
                    true,
                    val.is_string() ? val.get<std::string>() : val.dump(),
                    std::nullopt
                };
            } catch (const std::exception& e) {
                return ToolResult{false, "", std::string("json_query error: ") + e.what()};
            }
        });
}

} // namespace agent::tools