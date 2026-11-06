#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace agent::core {

/**
 * @brief Cấu trúc lưu trữ một Skill được nạp từ file Markdown (.md)
 */
struct Skill {
    std::string name;                      // Tên skill (lấy từ tên file hoặc tiêu đề)
    std::vector<std::string> keywords;     // Danh sách từ khóa kích hoạt skill
    std::string markdown_content;          // Nội dung hướng dẫn chi tiết cho Agent
};

/**
 * @brief Lớp quét thư mục skills/ bằng std::filesystem và chọn skill theo keyword matching
 */
class SkillLoader {
public:
    explicit SkillLoader(std::filesystem::path skills_dir = "skills");
    ~SkillLoader() = default;

    /**
     * @brief Quét thư mục skills_dir_ và nạp tất cả các file có đuôi .md
     * @return Số lượng skill nạp thành công
     */
    size_t loadAllSkills();

    /**
     * @brief Chọn các skill phù hợp với mô tả task dựa trên keyword matching
     */
    [[nodiscard]] std::vector<Skill> selectSkillsForTask(std::string_view task_description) const;

    /**
     * @brief Tạo đoạn văn bản tổng hợp các skill phù hợp để inject vào System Prompt
     */
    [[nodiscard]] std::string buildSkillPromptSection(std::string_view task_description) const;

    /**
     * @brief Lấy toàn bộ danh sách skill đã nạp
     */
    [[nodiscard]] const std::vector<Skill>& getLoadedSkills() const noexcept {
        return loaded_skills_;
    }

private:
    std::filesystem::path skills_dir_;
    std::vector<Skill> loaded_skills_;

    [[nodiscard]] static std::string toLower(std::string_view str);
    [[nodiscard]] static std::vector<std::string> extractKeywords(
        const std::string& filename_stem,
        const std::string& content);
};

} // namespace agent::core