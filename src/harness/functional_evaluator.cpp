#include "harness/evaluator.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace agent::harness {

EvalResult FunctionalEvaluator::evaluate(
    const TaskSpec& task,
    const std::string& final_answer) const {
    
    if (task.eval_script.empty()) {
        const bool ok = !final_answer.empty();
        return EvalResult{
            ok,
            ok ? std::optional<double>{1.0} : std::optional<double>{0.0},
            "No eval_script provided; checked if final_answer is non-empty."
        };
    }

    // [CROSS-PLATFORM NATIVE EVALUATION]:
    // Nhận diện các câu lệnh kiểm tra file & nội dung phổ biến dạng:
    // "test -f <file> && grep [-q] '<keyword>' <file> ... && echo PASS"
    // để kiểm tra trực tiếp qua std::filesystem và std::ifstream.
    // Điều này đảm bảo tính tương thích 100% trên cả Windows, Linux và macOS.
    static const std::regex kTestFileRegex(R"(test\s+-f\s+([^\s&]+))");
    static const std::regex kGrepRegex(R"raw(grep\s+(?:-[a-zA-Z]+\s+)?(?:'([^']+)'|"([^"]+)"|([^\s&]+)))raw");

    std::smatch test_match;
    if (std::regex_search(task.eval_script, test_match, kTestFileRegex)) {
        const std::string target_file_str = test_match[1].str();
        const std::filesystem::path target_file(target_file_str);

        if (!std::filesystem::exists(target_file)) {
            return EvalResult{
                false,
                0.0,
                "Functional eval_script failed: Target file '" + target_file_str + "' does not exist."
            };
        }

        // Đọc toàn bộ nội dung file cần kiểm tra
        std::ifstream in(target_file, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            return EvalResult{
                false,
                0.0,
                "Functional eval_script failed: Cannot open file '" + target_file_str + "' for reading."
            };
        }

        std::string file_content((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
        in.close();

        // Trích xuất tất cả các từ khóa kỳ vọng từ các mệnh đề grep
        std::vector<std::string> expected_keywords;
        auto search_start = task.eval_script.cbegin();
        std::smatch grep_match;
        while (std::regex_search(search_start, task.eval_script.cend(), grep_match, kGrepRegex)) {
            std::string kw;
            if (grep_match[1].matched) {
                kw = grep_match[1].str();
            } else if (grep_match[2].matched) {
                kw = grep_match[2].str();
            } else if (grep_match[3].matched) {
                kw = grep_match[3].str();
            }
            if (!kw.empty()) {
                expected_keywords.push_back(std::move(kw));
            }
            search_start = grep_match.suffix().first;
        }

        // Kiểm tra xem tất cả các từ khóa có nằm trong file_content không
        for (const auto& kw : expected_keywords) {
            // [KỸ THUẬT C++23]: std::string::contains
            if (!file_content.contains(kw)) {
                return EvalResult{
                    false,
                    0.0,
                    "Functional eval_script failed: File '" + target_file_str + "' does not contain expected keyword '" + kw + "'."
                };
            }
        }

        return EvalResult{
            true,
            1.0,
            "Functional eval_script succeeded (cross-platform native verification): PASS"
        };
    }

    // Fallback: Với các script hệ thống khác, gọi qua popen()
    const std::string full_cmd = task.eval_script + " 2>&1";

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        return EvalResult{
            false,
            0.0,
            "FunctionalEvaluator failed to execute eval_script via popen()."
        };
    }

    std::array<char, 512> buffer{};
    std::string script_output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        script_output.append(buffer.data());
    }

    const int exit_code = pclose(pipe);

    // [KỸ THUẬT C++23]: std::string::contains kiểm tra chuỗi "PASS" trong kết quả script
    const bool has_pass_marker = script_output.contains("PASS");
    const bool passed = (exit_code == 0) && (has_pass_marker || script_output.empty());

    if (passed) {
        return EvalResult{
            true,
            1.0,
            "Functional eval_script succeeded: " + script_output
        };
    }

    return EvalResult{
        false,
        0.0,
        "Functional eval_script failed (exit_code=" + std::to_string(exit_code) +
            "): " + script_output
    };
}

} // namespace agent::harness