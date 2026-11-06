#include "agent/agent_loop.h"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <type_traits>

namespace agent::core {

AgentLoop::AgentLoop(std::shared_ptr<client::LLMClient> llm_client,
                     std::shared_ptr<tools::ToolRegistry> tool_registry,
                     SkillLoader skill_loader,
                     int max_steps)
    : client_(std::move(llm_client)),
      tool_registry_(std::move(tool_registry)),
      skill_loader_(std::move(skill_loader)),
      max_steps_(max_steps) {
    skill_loader_.loadAllSkills();
}

std::string AgentLoop::buildSystemPrompt(const std::string& task_instruction) const {
    std::ostringstream oss;
    oss << "You are an autonomous ReAct AI Agent built in C++.\n"
        << "To solve the user's task, follow the loop: Thought -> Action -> Observation.\n\n"
        << "RESPONSE FORMAT RULES:\n"
        << "Always respond with a single valid JSON object in one of the two formats below:\n"
        << "1. When you need to call a tool:\n"
        << "{\n"
        << "  \"thought\": \"Explain your reasoning here\",\n"
        << "  \"action\": {\n"
        << "    \"type\": \"tool_call\",\n"
        << "    \"tool\": \"<tool_name>\",\n"
        << "    \"args\": <JSON object or string arguments for the tool>\n"
        << "  }\n"
        << "}\n\n"
        << "2. When the task is completely finished:\n"
        << "{\n"
        << "  \"thought\": \"Task is complete because...\",\n"
        << "  \"action\": {\n"
        << "    \"type\": \"done\",\n"
        << "    \"answer\": \"<your final comprehensive answer>\"\n"
        << "  }\n"
        << "}\n\n";

    if (tool_registry_) {
        oss << tool_registry_->generateToolsPrompt() << "\n";
    }

    const std::string skills_section = skill_loader_.buildSkillPromptSection(task_instruction);
    if (!skills_section.empty()) {
        oss << skills_section;
    }

    return oss.str();
}

std::string AgentLoop::observe(const tools::ToolResult& last_tool_result, int step_id) {
    std::ostringstream oss;
    oss << "[Observation Step " << step_id << "]\n";
    if (last_tool_result.success) {
        oss << "Status: SUCCESS\nOutput:\n" << last_tool_result.output;
    } else {
        oss << "Status: ERROR\nError Message: "
            << last_tool_result.error_message.value_or("Unknown tool execution error")
            << "\nPartial Output:\n" << last_tool_result.output;
    }
    return oss.str();
}

std::expected<client::LLMResponse, client::LLMError> AgentLoop::think(
    const std::vector<client::Message>& conversation_history) {
    if (!client_) {
        return std::unexpected(client::LLMError{
            client::LLMErrorCode::Unknown,
            "LLMClient pointer is null in AgentLoop."
        });
    }
    return client_->chat(conversation_history);
}

std::optional<tools::ToolResult> AgentLoop::act(const AgentAction& action) {
    // [KỸ THUẬT C++17 BẮT BUỘC - MỤC V]: Dùng std::visit kết hợp if constexpr để xử lý std::variant
    return std::visit([this](const auto& act_obj) -> std::optional<tools::ToolResult> {
        using T = std::decay_t<decltype(act_obj)>;

        if constexpr (std::is_same_v<T, ToolCallAction>) {
            if (!tool_registry_) {
                return tools::ToolResult{false, "", "ToolRegistry is not initialized."};
            }
            return tool_registry_->executeTool(act_obj.tool_name, act_obj.args);
        }
        else if constexpr (std::is_same_v<T, ClickAction>) {
            // Hỗ trợ mở rộng GUI Agent: thực thi click chuột qua xdotool hoặc ExecTool
            const std::string cmd = "{\"command\": \"xdotool mousemove " +
                                    std::to_string(act_obj.x) + " " +
                                    std::to_string(act_obj.y) + " click 1\"}";
            return tool_registry_ ? tool_registry_->executeTool("exec", cmd) : std::nullopt;
        }
        else if constexpr (std::is_same_v<T, TypeTextAction>) {
            const std::string cmd = "{\"command\": \"xdotool type -- '" + act_obj.text + "'\"}";
            return tool_registry_ ? tool_registry_->executeTool("exec", cmd) : std::nullopt;
        }
        else if constexpr (std::is_same_v<T, KeyPressAction>) {
            const std::string cmd = "{\"command\": \"xdotool key " + act_obj.key + "\"}";
            return tool_registry_ ? tool_registry_->executeTool("exec", cmd) : std::nullopt;
        }
        else if constexpr (std::is_same_v<T, DoneAction>) {
            return std::nullopt;
        }
    }, action);
}

ParsedResponse AgentLoop::parseLLMOutput(const std::string& raw_content) {
    using json = nlohmann::json;

    // 1. Tìm khối JSON hợp lệ đầu tiên trong phản hồi của LLM (hỗ trợ cả ```json ... ```)
    const auto first_brace = raw_content.find('{');
    const auto last_brace = raw_content.rfind('}');

    if (first_brace != std::string::npos && last_brace != std::string::npos && last_brace > first_brace) {
        const std::string json_candidate = raw_content.substr(first_brace, last_brace - first_brace + 1);
        try {
            const auto parsed = json::parse(json_candidate);
            const std::string thought = parsed.value("thought", "");

            if (parsed.contains("action") && parsed["action"].is_object()) {
                const auto& act_json = parsed["action"];
                const std::string type = act_json.value("type", "tool_call");

                if (type == "done" || type == "final_answer") {
                    const std::string ans = act_json.value("answer", act_json.value("final_answer", ""));
                    return ParsedResponse{thought, DoneAction{ans}};
                }
                if (type == "click") {
                    return ParsedResponse{
                        thought,
                        ClickAction{act_json.value("x", 0), act_json.value("y", 0)}
                    };
                }
                if (type == "type_text") {
                    return ParsedResponse{thought, TypeTextAction{act_json.value("text", "")}};
                }
                if (type == "key_press") {
                    return ParsedResponse{thought, KeyPressAction{act_json.value("key", "Return")}};
                }

                // Mặc định là tool_call
                const std::string tool_name = act_json.value("tool", act_json.value("name", ""));
                std::string args_str;
                if (act_json.contains("args")) {
                    args_str = act_json["args"].is_string()
                                   ? act_json["args"].get<std::string>()
                                   : act_json["args"].dump();
                }
                if (!tool_name.empty()) {
                    return ParsedResponse{thought, ToolCallAction{tool_name, args_str}};
                }
            }
        } catch (...) {
            // Chuyển sang nhánh fallback Regex bên dưới nếu parse JSON thất bại
        }
    }

    // 2. Fallback: Phân tích định dạng ReAct văn bản thuần bằng std::regex
    // Ví dụ:
    // Thought: Cần tính 15*17
    // Action: calculator
    // Action Input: 15*17
    std::smatch thought_match;
    std::string extracted_thought;
    static const std::regex kThoughtRegex(R"(Thought:\s*(.+))", std::regex::icase);
    if (std::regex_search(raw_content, thought_match, kThoughtRegex)) {
        extracted_thought = thought_match[1].str();
    }

    std::smatch action_match;
    std::smatch input_match;
    static const std::regex kActionRegex(R"(Action:\s*([a-zA-Z0-9_]+))", std::regex::icase);
    static const std::regex kInputRegex(R"(Action Input:\s*(.+))", std::regex::icase);

    if (std::regex_search(raw_content, action_match, kActionRegex)) {
        const std::string tool_name = action_match[1].str();
        if (tool_name != "Done" && tool_name != "Final" && tool_name != "None") {
            std::string tool_args;
            if (std::regex_search(raw_content, input_match, kInputRegex)) {
                tool_args = input_match[1].str();
            }
            return ParsedResponse{extracted_thought, ToolCallAction{tool_name, tool_args}};
        }
    }

    // 3. Nếu không có tool call nào, coi toàn bộ câu trả lời là kết quả cuối cùng (DoneAction)
    return ParsedResponse{
        extracted_thought.empty() ? "Providing final response directly." : extracted_thought,
        DoneAction{raw_content}
    };
}

AgentRunResult AgentLoop::run(
    const std::string& task_instruction,
    const std::vector<std::string>& initial_images_base64) {
    
    const auto run_start = std::chrono::steady_clock::now();
    loop_detector_.reset();

    AgentRunResult final_result{};
    std::vector<client::Message> history;

    // 1. Khởi tạo System Prompt (có gắn kèm Skill và danh sách Tool)
    history.push_back(client::Message{
        "system",
        buildSystemPrompt(task_instruction),
        {}
    });

    // 2. Đưa yêu cầu của User (kèm ảnh Base64 nếu là task Multimodal) vào history
    history.push_back(client::Message{
        "user",
        task_instruction,
        initial_images_base64
    });

    // 3. Vòng lặp ReAct giới hạn bởi max_steps_
    for (int step = 0; step < max_steps_; ++step) {
        // BƯỚC THINK: Gọi LLM suy luận
        auto llm_exp = think(history);
        if (!llm_exp.has_value()) {
            final_result.final_answer = "LLM Error: " + llm_exp.error().message;
            break;
        }

        const auto& llm_resp = llm_exp.value();
        final_result.total_tokens += llm_resp.total_tokens;

        // Lưu phản hồi của assistant vào lịch sử hội thoại
        history.push_back(client::Message{"assistant", llm_resp.content, {}});

        // Phân tích Thought và Action
        ParsedResponse parsed = parseLLMOutput(llm_resp.content);

        StepEvent event{};
        event.step_id = step;
        event.thought = parsed.thought;
        event.tokens_used = llm_resp.total_tokens;
        event.latency_ms = llm_resp.latency_ms;

        // Kiểm tra nếu Agent báo hoàn thành (DoneAction)
        if (const auto* done_ptr = std::get_if<DoneAction>(&parsed.action)) {
            event.action_type = "done";
            event.tool_result = done_ptr->final_answer;
            if (step_hook_) {
                step_hook_(event);
            }

            final_result.completed = true;
            final_result.final_answer = done_ptr->final_answer;
            break;
        }

        // Trích xuất thông tin Action để kiểm tra LoopDetector
        if (const auto* tc_ptr = std::get_if<ToolCallAction>(&parsed.action)) {
            event.action_type = "tool_call";
            event.tool_name = tc_ptr->tool_name;
            event.tool_args = tc_ptr->args;
        } else if (const auto* click_ptr = std::get_if<ClickAction>(&parsed.action)) {
            event.action_type = "click";
            event.tool_name = "gui_click";
            event.tool_args = std::to_string(click_ptr->x) + "," + std::to_string(click_ptr->y);
        } else if (const auto* type_ptr = std::get_if<TypeTextAction>(&parsed.action)) {
            event.action_type = "type_text";
            event.tool_name = "gui_type";
            event.tool_args = type_ptr->text;
        } else if (const auto* key_ptr = std::get_if<KeyPressAction>(&parsed.action)) {
            event.action_type = "key_press";
            event.tool_name = "gui_key";
            event.tool_args = key_ptr->key;
        }

        // Kiểm tra Loop Detection (Generic Repeat & Ping-Pong)
        const LoopCheckResult loop_check = loop_detector_.recordAction(event.tool_name, event.tool_args);
        if (loop_check.status == LoopStatus::CriticalRepeat ||
            loop_check.status == LoopStatus::CriticalPingPong) {
            std::cerr << loop_check.message << " Stopping AgentLoop gracefully.\n";
            event.tool_result = loop_check.message;
            if (step_hook_) {
                step_hook_(event);
            }
            final_result.stopped_by_loop = true;
            final_result.final_answer = loop_check.message;
            break;
        }

        // BƯỚC ACT: Thực thi công cụ
        const auto tool_result_opt = act(parsed.action);
        tools::ToolResult actual_result = tool_result_opt.value_or(
            tools::ToolResult{false, "", "Tool returned empty optional result."}
        );

        event.tool_result = actual_result.success
                                ? actual_result.output
                                : ("ERROR: " + actual_result.error_message.value_or(actual_result.output));

        // Phát sự kiện qua Hook cho HarnessRunner ghi lại Trajectory
        if (step_hook_) {
            step_hook_(event);
        }

        // BƯỚC OBSERVE: Đưa kết quả quan sát (kèm cảnh báo lặp nếu ở mức Warning) vào history
        std::string observation_msg = observe(actual_result, step);
        if (loop_check.status == LoopStatus::WarningRepeat ||
            loop_check.status == LoopStatus::WarningPingPong) {
            std::cerr << loop_check.message << "\n";
            observation_msg += "\n" + loop_check.message +
                               " Please try a different approach or conclude if finished.";
        }

        history.push_back(client::Message{"user", observation_msg, {}});
    }

    if (!final_result.completed && !final_result.stopped_by_loop &&
        final_result.final_answer.empty()) {
        final_result.reached_max_steps = true;
        final_result.final_answer = "Agent reached maximum step limit (" +
                                    std::to_string(max_steps_) + ") without concluding.";
    }

    const auto run_end = std::chrono::steady_clock::now();
    final_result.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        run_end - run_start).count();

    return final_result;
}

} // namespace agent::core