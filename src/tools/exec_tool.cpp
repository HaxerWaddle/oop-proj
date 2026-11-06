#include "tools/tool.h"

#include <array>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>

namespace agent::tools {

std::string ExecTool::description() const {
    return "Run a shell command and return stdout/stderr. "
           "Args can be raw command string or JSON: {\"command\": \"ls -la\"}.";
}

std::optional<ToolResult> ExecTool::execute(const std::string& args_json) {
    std::string command = args_json;

    // Hỗ trợ cả JSON format {"command": "..."} lẫn chuỗi lệnh trực tiếp
    if (!args_json.empty() && args_json.front() == '{') {
        try {
            const auto parsed = nlohmann::json::parse(args_json);
            if (parsed.contains("command") && parsed["command"].is_string()) {
                command = parsed["command"].get<std::string>();
            } else if (parsed.contains("cmd") && parsed["cmd"].is_string()) {
                command = parsed["cmd"].get<std::string>();
            }
        } catch (...) {
            // Giữ nguyên chuỗi gốc nếu không phải JSON hợp lệ
        }
    }

    if (command.empty()) {
        return ToolResult{false, "", "Empty shell command provided to ExecTool."};
    }

    // Chuyển hướng stderr sang stdout để bắt trọn thông báo lỗi
    const std::string full_command = command + " 2>&1";

    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) {
        return ToolResult{false, "", "Failed to spawn shell process via popen()."};
    }

    std::array<char, 512> buffer{};
    std::string output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output.append(buffer.data());
    }

    const int exit_code = pclose(pipe);
    if (exit_code != 0) {
        return ToolResult{
            false,
            output,
            "Command exited with non-zero status: " + std::to_string(exit_code)
        };
    }

    return ToolResult{true, output, std::nullopt};
}

} // namespace agent::tools