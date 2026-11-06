#include "tools/tool.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace agent::tools {

std::string FileTool::description() const {
    return "Read or write files on disk. "
           "Args JSON for reading: {\"action\": \"read\", \"path\": \"result.txt\"}. "
           "Args JSON for writing: {\"action\": \"write\", \"path\": \"result.txt\", \"content\": \"255\"}.";
}

std::optional<ToolResult> FileTool::execute(const std::string& args_json) {
    using json = nlohmann::json;

    std::string action = "read";
    std::string file_path_str;
    std::string content;

    try {
        if (!args_json.empty() && args_json.front() == '{') {
            const auto parsed = json::parse(args_json);
            action = parsed.value("action", "read");
            file_path_str = parsed.value("path", parsed.value("filepath", ""));
            content = parsed.value("content", "");
        } else {
            // Nếu truyền chuỗi thuần thì mặc định là đọc file có đường dẫn đó
            file_path_str = args_json;
        }
    } catch (const std::exception& e) {
        return ToolResult{false, "", std::string("Invalid JSON args for FileTool: ") + e.what()};
    }

    if (file_path_str.empty()) {
        return ToolResult{false, "", "File path is required for FileTool."};
    }

    const std::filesystem::path target_path(file_path_str);
    std::error_code ec;

    // 1. Thao tác GHI FILE (write / write_file)
    if (action == "write" || action == "write_file") {
        if (target_path.has_parent_path()) {
            std::filesystem::create_directories(target_path.parent_path(), ec);
        }

        std::ofstream out_file(target_path, std::ios::out | std::ios::trunc);
        if (!out_file.is_open()) {
            return ToolResult{false, "", "Failed to open file for writing: " + target_path.string()};
        }
        out_file << content;
        out_file.close();

        return ToolResult{
            true,
            "Successfully wrote " + std::to_string(content.size()) + " bytes to " + target_path.string(),
            std::nullopt
        };
    }

    // 2. Thao tác ĐỌC FILE (read / read_file)
    if (action == "read" || action == "read_file") {
        if (!std::filesystem::exists(target_path, ec)) {
            return ToolResult{false, "", "File does not exist: " + target_path.string()};
        }

        std::ifstream in_file(target_path, std::ios::in);
        if (!in_file.is_open()) {
            return ToolResult{false, "", "Failed to open file for reading: " + target_path.string()};
        }

        std::ostringstream ss;
        ss << in_file.rdbuf();
        return ToolResult{true, ss.str(), std::nullopt};
    }

    return ToolResult{false, "", "Unsupported FileTool action: " + action};
}

} // namespace agent::tools