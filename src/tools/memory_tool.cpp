#include "tools/tool.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <sstream>

namespace agent::tools {

MemoryTool::MemoryTool(std::string db_path) : db_path_(std::move(db_path)) {
    (void)ensureTableInitialized();
}

std::string MemoryTool::description() const {
    return "Save or search persistent memory using SQLite. "
           "Save args JSON: {\"action\": \"save\", \"key\": \"user_pref\", \"content\": \"likes C++26\"}. "
           "Search args JSON: {\"action\": \"search\", \"query\": \"C++26\"}.";
}

bool MemoryTool::ensureTableInitialized() const {
    sqlite3* raw_db = nullptr;
    if (sqlite3_open(db_path_.c_str(), &raw_db) != SQLITE_OK) {
        if (raw_db) sqlite3_close(raw_db);
        return false;
    }
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw_db, &sqlite3_close);

    constexpr const char* kCreateTableSql =
        "CREATE TABLE IF NOT EXISTS memories ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "mem_key TEXT NOT NULL, "
        "content TEXT NOT NULL, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char* err_msg = nullptr;
    const int rc = sqlite3_exec(db.get(), kCreateTableSql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    return true;
}

std::optional<ToolResult> MemoryTool::execute(const std::string& args_json) {
    if (!ensureTableInitialized()) {
        return ToolResult{false, "", "Failed to initialize SQLite database at: " + db_path_};
    }

    std::string action = "search";
    std::string key;
    std::string content;
    std::string query;

    try {
        if (!args_json.empty() && args_json.front() == '{') {
            const auto parsed = nlohmann::json::parse(args_json);
            action = parsed.value("action", "search");
            key = parsed.value("key", "general");
            content = parsed.value("content", parsed.value("value", ""));
            query = parsed.value("query", parsed.value("key", ""));
        } else {
            query = args_json;
        }
    } catch (const std::exception& e) {
        return ToolResult{false, "", std::string("Invalid JSON args for MemoryTool: ") + e.what()};
    }

    sqlite3* raw_db = nullptr;
    if (sqlite3_open(db_path_.c_str(), &raw_db) != SQLITE_OK) {
        if (raw_db) sqlite3_close(raw_db);
        return ToolResult{false, "", "Cannot open SQLite database."};
    }
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw_db, &sqlite3_close);

    // 1. Lưu bộ nhớ (save / memory_save)
    if (action == "save" || action == "memory_save") {
        if (content.empty()) {
            return ToolResult{false, "", "Memory 'content' cannot be empty when saving."};
        }

        constexpr const char* kInsertSql = "INSERT INTO memories (mem_key, content) VALUES (?, ?);";
        sqlite3_stmt* raw_stmt = nullptr;
        if (sqlite3_prepare_v2(db.get(), kInsertSql, -1, &raw_stmt, nullptr) != SQLITE_OK) {
            return ToolResult{false, "", std::string("SQLite prepare error: ") + sqlite3_errmsg(db.get())};
        }
        // [KỸ THUẬT C++26]: Biến placeholder '_' làm RAII guard giải phóng sqlite3_stmt
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> _(raw_stmt, &sqlite3_finalize);

        sqlite3_bind_text(raw_stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(raw_stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(raw_stmt) != SQLITE_DONE) {
            return ToolResult{false, "", std::string("SQLite insert failed: ") + sqlite3_errmsg(db.get())};
        }

        return ToolResult{true, "Saved memory under key '" + key + "'.", std::nullopt};
    }

    // 2. Truy vấn bộ nhớ (search / memory_search)
    if (action == "search" || action == "memory_search") {
        constexpr const char* kSearchSql =
            "SELECT mem_key, content, created_at FROM memories "
            "WHERE mem_key LIKE ? OR content LIKE ? ORDER BY id DESC LIMIT 10;";

        sqlite3_stmt* raw_stmt = nullptr;
        if (sqlite3_prepare_v2(db.get(), kSearchSql, -1, &raw_stmt, nullptr) != SQLITE_OK) {
            return ToolResult{false, "", std::string("SQLite prepare error: ") + sqlite3_errmsg(db.get())};
        }
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> _(raw_stmt, &sqlite3_finalize);

        const std::string pattern = "%" + query + "%";
        sqlite3_bind_text(raw_stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(raw_stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);

        std::ostringstream oss;
        int count = 0;
        while (sqlite3_step(raw_stmt) == SQLITE_ROW) {
            const auto* k = reinterpret_cast<const char*>(sqlite3_column_text(raw_stmt, 0));
            const auto* c = reinterpret_cast<const char*>(sqlite3_column_text(raw_stmt, 1));
            const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(raw_stmt, 2));
            oss << "[" << (t ? t : "") << "] " << (k ? k : "") << ": " << (c ? c : "") << "\n";
            ++count;
        }

        if (count == 0) {
            return ToolResult{true, "No matching memories found for query: '" + query + "'.", std::nullopt};
        }
        return ToolResult{true, oss.str(), std::nullopt};
    }

    return ToolResult{false, "", "Unknown MemoryTool action: " + action};
}

} // namespace agent::tools