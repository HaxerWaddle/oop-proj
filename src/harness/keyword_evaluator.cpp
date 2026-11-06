#include "harness/evaluator.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <sstream>

namespace agent::harness {

// ==============================================================================
// 1. Triển khai KeywordEvaluator
// ==============================================================================

KeywordEvaluator::KeywordEvaluator(std::vector<std::string> default_keywords)
    : default_keywords_(std::move(default_keywords)) {}

std::string KeywordEvaluator::toLower(std::string str) {
    std::ranges::transform(str, str.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

EvalResult KeywordEvaluator::evaluate(
    const TaskSpec& task,
    const std::string& final_answer) const {
    
    const auto& keywords = !task.expected_keywords.empty()
                               ? task.expected_keywords
                               : default_keywords_;

    if (keywords.empty()) {
        const bool non_empty = !final_answer.empty();
        return EvalResult{
            non_empty,
            non_empty ? std::optional<double>{1.0} : std::optional<double>{0.0},
            non_empty ? "No keywords specified; answer is non-empty." : "Final answer is empty."
        };
    }

    const std::string lower_ans = toLower(final_answer);

    // [KỸ THUẬT C++20 & C++23]: std::ranges::count_if + std::string::contains
    const auto matched_count = std::ranges::count_if(keywords, [&](const std::string& kw) {
        return !kw.empty() && lower_ans.contains(toLower(kw));
    });

    const double ratio = static_cast<double>(matched_count) / static_cast<double>(keywords.size());
    const bool all_matched = (static_cast<size_t>(matched_count) == keywords.size());

    std::ostringstream oss;
    oss << "Matched " << matched_count << "/" << keywords.size() << " expected keywords.";

    return EvalResult{
        all_matched,
        ratio, // Gán vào std::optional<double> partial_score
        oss.str()
    };
}

// ==============================================================================
// 2. Triển khai VLMEvaluator (LLM-as-a-Judge / VLM Judge)
// ==============================================================================

VLMEvaluator::VLMEvaluator(std::shared_ptr<client::LLMClient> vlm_client)
    : vlm_client_(std::move(vlm_client)) {}

EvalResult VLMEvaluator::evaluate(
    const TaskSpec& task,
    const std::string& final_answer) const {
    
    if (!vlm_client_) {
        return EvalResult{false, std::nullopt, "VLMEvaluator has no LLMClient configured."};
    }

    std::ostringstream prompt;
    prompt << "You are a strict automated grading judge.\n"
           << "Task Instruction: " << task.instruction << "\n"
           << "Expected Criteria: " << task.description << "\n"
           << "Agent Final Answer: " << final_answer << "\n\n"
           << "Reply with 'PASS' on the first line if the Agent accurately fulfilled the task, "
           << "otherwise reply with 'FAIL' followed by a brief reason.";

    const std::vector<client::Message> msgs = {
        {"user", prompt.str(), {}}
    };

    auto resp_exp = vlm_client_->chat(msgs);
    if (!resp_exp.has_value()) {
        return EvalResult{
            false,
            std::nullopt,
            "VLMEvaluator API error: " + resp_exp.error().message
        };
    }

    const std::string& verdict = resp_exp.value().content;
    const bool passed = verdict.contains("PASS") && !verdict.contains("FAIL");

    return EvalResult{
        passed,
        passed ? std::optional<double>{1.0} : std::optional<double>{0.0},
        verdict
    };
}

} // namespace agent::harness