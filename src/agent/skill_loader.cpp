#include "agent/skill_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <sstream>

namespace agent::core {

SkillLoader::SkillLoader(std::filesystem::path skills_dir)
    : skills_dir_(std::move(skills_dir)) {}

std::string SkillLoader::toLower(std::string_view str) {
    std::string lower_str(str);
    std::ranges::transform(lower_str, lower_str.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower_str;
}

std::vector<std::string> SkillLoader::extractKeywords(
    const std::string& filename_stem,
    const std::string& content) {
    std::vector<std::string> keywords;

    // 1. Tách các từ trong tên file (ví dụ: "task_planner" -> "task", "planner")
    std::stringstream stem_ss(filename_stem);
    std::string token;
    while (std::getline(stem_ss, token, '_')) {
        if (!token.empty()) {
            keywords.push_back(toLower(token));
        }
    }

    // 2. Tìm dòng khai báo "Keywords:" hoặc "<!-- keywords: ... -->" trong file .md
    std::istringstream line_ss(content);
    std::string line;
    while (std::getline(line_ss, line)) {
        const std::string lower_line = toLower(line);
        const auto pos = lower_line.find("keywords:");
        if (pos != std::string::npos) {
            std::string raw_kw = lower_line.substr(pos + 9);
            // Loại bỏ ký tự đóng comment markdown nếu có
            if (const auto end_pos = raw_kw.find("-->"); end_pos != std::string::npos) {
                raw_kw = raw_kw.substr(0, end_pos);
            }

            std::stringstream kw_ss(raw_kw);
            std::string kw;
            while (std::getline(kw_ss, kw, ',')) {
                // Trim khoảng trắng hai đầu
                const auto first = kw.find_first_not_of(" \t\r\n");
                if (first != std::string::npos) {
                    const auto last = kw.find_last_not_of(" \t\r\n");
                    keywords.push_back(kw.substr(first, last - first + 1));
                }
            }
            break;
        }
    }

    return keywords;
}

size_t SkillLoader::loadAllSkills() {
    loaded_skills_.clear();

    // [KỸ THUẬT C++17]: std::filesystem kiểm tra và duyệt thư mục skills/
    std::error_code ec;
    if (!std::filesystem::exists(skills_dir_, ec) || !std::filesystem::is_directory(skills_dir_, ec)) {
        return 0;
    }

    for (const auto& entry : std::filesystem::directory_iterator(skills_dir_, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() != ".md") {
            continue;
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            continue;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        if (content.empty()) {
            continue;
        }

        Skill skill;
        skill.name = path.stem().string();
        skill.keywords = extractKeywords(skill.name, content);
        skill.markdown_content = std::move(content);

        loaded_skills_.push_back(std::move(skill));
    }

    return loaded_skills_.size();
}

std::vector<Skill> SkillLoader::selectSkillsForTask(std::string_view task_description) const {
    std::vector<Skill> selected;
    if (loaded_skills_.empty()) {
        return selected;
    }

    const std::string lower_task = toLower(task_description);

    for (const auto& skill : loaded_skills_) {
        // [KỸ THUẬT C++20 & C++23]: std::ranges::any_of + std::string::contains
        const bool matched = std::ranges::any_of(skill.keywords, [&](const std::string& kw) {
            return !kw.empty() && lower_task.contains(kw);
        });

        if (matched) {
            selected.push_back(skill);
        }
    }

    // Nếu không khớp từ khóa cụ thể nào, ưu tiên nạp "task_planner" làm skill mặc định
    if (selected.empty()) {
        for (const auto& skill : loaded_skills_) {
            if (skill.name == "task_planner") {
                selected.push_back(skill);
                break;
            }
        }
    }

    return selected;
}

std::string SkillLoader::buildSkillPromptSection(std::string_view task_description) const {
    const auto matched_skills = selectSkillsForTask(task_description);
    if (matched_skills.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "\n=== ACTIVATED AGENT SKILLS ===\n";
    for (const auto& [name, _, content] : matched_skills) {
        // [KỸ THUẬT C++26]: Dùng biến placeholder '_' bỏ qua trường keywords khi unpack
        oss << "--- Skill: " << name << " ---\n"
            << content << "\n\n";
    }
    return oss.str();
}

} // namespace agent::core