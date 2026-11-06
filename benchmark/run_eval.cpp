#include "agent/agent_loop.h"
#include "client/llm_client.h"
#include "client/ollama_client.h"
#include "harness/evaluator.h"
#include "harness/harness_runner.h"
#include "tools/tool_registry.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// [KỸ THUẬT C++23]: Sử dụng <print> (std::println) nếu trình biên dịch hỗ trợ đầy đủ
#if __has_include(<print>)
#include <print>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

/**
 * @brief Hàm in dòng văn bản bọc std::println (C++23) hoặc std::cout
 */
void printBannerLine(std::string_view text) {
#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L && !defined(_WIN32)
    std::println("{}", text);
#else
    std::cout << text << '\n';
#endif
}

/**
 * @brief Lớp MockScriptedLLMClient kế thừa từ LLMClient (Chứng minh LSP & Abstraction Layer Mục 4.4).
 * Cho phép kiểm thử toàn bộ 5 Tools, SkillLoader, LoopDetector, Hook và Evaluator ở chế độ Offline
 * (--mock) trước khi chạy với Ollama Server thực tế.
 */
class MockScriptedLLMClient final : public agent::client::LLMClient {
public:
    MockScriptedLLMClient() {
        config_.model_name = "mock-react-verifier:1.0";
    }

    void setConfig(const agent::client::LLMConfig& config) override { config_ = config; }
    [[nodiscard]] const agent::client::LLMConfig& getConfig() const noexcept override { return config_; }

    [[nodiscard]] std::expected<agent::client::LLMResponse, agent::client::LLMError> chat(
        const std::vector<agent::client::Message>& messages) override {
        
        if (messages.size() < 2) {
            return std::unexpected(agent::client::LLMError{
                agent::client::LLMErrorCode::Unknown,
                "Conversation history too short."
            });
        }

        const std::string& task_prompt = messages[1].content;
        const size_t assistant_turns = (messages.size() - 2) / 2;

        std::string reply_json;

        // Kịch bản ReAct tự động hóa cho 10 task trong benchmark/tasks.json
        // Hỗ trợ cả khớp chuỗi UTF-8 lẫn các tham số CLI không phụ thuộc vào dấu
        if ((task_prompt.contains("15") && task_prompt.contains("17")) || task_prompt.contains("result.txt")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Tính 15 * 17 bằng calculator trước.","action":{"type":"tool_call","tool":"calculator","args":"15*17"}})";
            } else if (assistant_turns == 1) {
                reply_json = R"({"thought":"Đã có kết quả 255, lưu vào file result.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"result.txt","content":"255"}}})";
            } else {
                reply_json = R"({"thought":"Đã ghi file thành công.","action":{"type":"done","answer":"Kết quả 15 * 17 = 255 đã được lưu vào result.txt"}})";
            }
        }
        else if (task_prompt.contains("Hello_OOP_Agent_2026") || task_prompt.contains("greeting.txt")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Ghi chuỗi vào greeting.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"greeting.txt","content":"Hello_OOP_Agent_2026"}}})";
            } else {
                reply_json = R"({"thought":"Hoàn tất ghi greeting.txt.","action":{"type":"done","answer":"Đã ghi Hello_OOP_Agent_2026 vào greeting.txt"}})";
            }
        }
        else if (task_prompt.contains("125 + 375") || task_prompt.contains("2^5")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Tính biểu thức bằng calculator.","action":{"type":"tool_call","tool":"calculator","args":"(125 + 375) / 25 + 2^5"}})";
            } else {
                reply_json = R"({"thought":"Đã nhận kết quả 52 từ calculator.","action":{"type":"done","answer":"Kết quả của biểu thức là 52."}})";
            }
        }
        else if (task_prompt.contains("datetime_info") || task_prompt.contains("ngày giờ") || task_prompt.contains("thời gian")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Gọi datetime_info để lấy ngày giờ hệ thống.","action":{"type":"tool_call","tool":"datetime_info","args":{}}})";
            } else {
                reply_json = R"({"thought":"Đã lấy được thời gian hệ thống.","action":{"type":"done","answer":"Thời gian hiện tại thuộc năm 2026."}})";
            }
        }
        else if ((task_prompt.contains("24") && task_prompt.contains("15")) || task_prompt.contains("mem_status.txt")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Bước 1: Tính 24 * 15.","action":{"type":"tool_call","tool":"calculator","args":"24*15"}})";
            } else if (assistant_turns == 1) {
                reply_json = R"({"thought":"Bước 2: Lưu 360 vào SQLite memory.","action":{"type":"tool_call","tool":"memory","args":{"action":"save","key":"score_threshold","content":"360"}}})";
            } else if (assistant_turns == 2) {
                reply_json = R"({"thought":"Bước 3: Ghi SAVED_360 vào mem_status.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"mem_status.txt","content":"SAVED_360"}}})";
            } else {
                reply_json = R"({"thought":"Hoàn thành cả 3 bước.","action":{"type":"done","answer":"Đã lưu 360 vào memory và ghi SAVED_360."}})";
            }
        }
        else if (task_prompt.contains("ollama_port.txt") || task_prompt.contains("11434")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Trích xuất khóa port bằng json_query.","action":{"type":"tool_call","tool":"json_query","args":{"json_str":"{\"host\":\"localhost\",\"port\":\"11434\"}","key":"port"}}})";
            } else if (assistant_turns == 1) {
                reply_json = R"({"thought":"Ghi port 11434 vào ollama_port.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"ollama_port.txt","content":"11434"}}})";
            } else {
                reply_json = R"({"thought":"Hoàn tất trích xuất và lưu port.","action":{"type":"done","answer":"Port 11434 đã được lưu vào ollama_port.txt."}})";
            }
        }
        else if (task_prompt.contains("tripled.txt") || task_prompt.contains("input_num.txt")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Tạo file input_num.txt qua exec.","action":{"type":"tool_call","tool":"exec","args":{"command":"echo 42 > input_num.txt"}}})";
            } else if (assistant_turns == 1) {
                reply_json = R"({"thought":"Đọc file input_num.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"read","path":"input_num.txt"}}})";
            } else if (assistant_turns == 2) {
                reply_json = R"({"thought":"Nhân 42 với 3 bằng calculator.","action":{"type":"tool_call","tool":"calculator","args":"42*3"}})";
            } else if (assistant_turns == 3) {
                reply_json = R"({"thought":"Ghi 126 vào tripled.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"tripled.txt","content":"126"}}})";
            } else {
                reply_json = R"({"thought":"Đã hoàn thành chuỗi 4 bước.","action":{"type":"done","answer":"Giá trị 126 đã được lưu vào tripled.txt."}})";
            }
        }
        else if ((task_prompt.contains("19") && task_prompt.contains("6")) || task_prompt.contains("level.txt")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Tính 19 * 6 bằng calculator.","action":{"type":"tool_call","tool":"calculator","args":"19*6"}})";
            } else if (assistant_turns == 1) {
                reply_json = R"({"thought":"114 > 100 nên ghi HIGH_114 vào level.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"level.txt","content":"HIGH_114"}}})";
            } else {
                reply_json = R"({"thought":"Đã ghi HIGH_114.","action":{"type":"done","answer":"19 * 6 = 114 (>100), đã ghi HIGH_114 vào level.txt."}})";
            }
        }
        else if (task_prompt.contains("audit_report.txt") || task_prompt.contains("audit_cwd")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Bước 1: Kiểm tra thư mục làm việc.","action":{"type":"tool_call","tool":"system_inspect","args":{}}})";
            } else if (assistant_turns == 1) {
                reply_json = R"({"thought":"Bước 2: Lưu thư mục vào SQLite memory.","action":{"type":"tool_call","tool":"memory","args":{"action":"save","key":"audit_cwd","content":"workspace_verified"}}})";
            } else if (assistant_turns == 2) {
                reply_json = R"({"thought":"Bước 3: Tìm kiếm lại trong SQLite memory.","action":{"type":"tool_call","tool":"memory","args":{"action":"search","query":"audit_cwd"}}})";
            } else if (assistant_turns == 3) {
                reply_json = R"({"thought":"Bước 4: Tính (18 * 18) - 24.","action":{"type":"tool_call","tool":"calculator","args":"(18*18)-24"}})";
            } else if (assistant_turns == 4) {
                reply_json = R"({"thought":"Bước 5: Ghi 300_VERIFIED vào audit_report.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"audit_report.txt","content":"300_VERIFIED"}}})";
            } else {
                reply_json = R"({"thought":"Hoàn tất quy trình kiểm toán 5 bước.","action":{"type":"done","answer":"Báo cáo 300_VERIFIED đã được lưu vào audit_report.txt."}})";
            }
        }
        else if (task_prompt.contains("pipeline_out.txt") || task_prompt.contains("final_metric")) {
            if (assistant_turns == 0) {
                reply_json = R"({"thought":"Trích xuất base từ JSON.","action":{"type":"tool_call","tool":"json_query","args":{"json_str":"{\"base\":\"12\"}","key":"base"}}})";
            } else if (assistant_turns == 1) {
                reply_json = R"({"thought":"Tính 12^2 + 56 bằng calculator.","action":{"type":"tool_call","tool":"calculator","args":"12^2 + 56"}})";
            } else if (assistant_turns == 2) {
                reply_json = R"({"thought":"Lưu 200 vào memory với key final_metric.","action":{"type":"tool_call","tool":"memory","args":{"action":"save","key":"final_metric","content":"200"}}})";
            } else if (assistant_turns == 3) {
                reply_json = R"({"thought":"Ghi METRIC=200 vào pipeline_out.txt.","action":{"type":"tool_call","tool":"file","args":{"action":"write","path":"pipeline_out.txt","content":"METRIC=200"}}})";
            } else {
                reply_json = R"({"thought":"Hoàn thành toàn bộ pipeline.","action":{"type":"done","answer":"Đã ghi METRIC=200 vào pipeline_out.txt."}})";
            }
        }
        else {
            reply_json = R"({"thought":"Trả lời trực tiếp.","action":{"type":"done","answer":"Task completed."}})";
        }

        return agent::client::LLMResponse{
            reply_json,
            120,
            64,
            184,
            15
        };
    }

private:
    agent::client::LLMConfig config_{};
};

struct CliOptions {
    std::string base_url{"http://localhost:11434"};
    std::string model_name{"qwen3-vl:7b"};
    std::string tasks_file{"benchmark/tasks.json"};
    std::string output_dir{"trajectories"};
    std::string single_prompt{};
    bool use_mock{false};
    bool use_sandbox{true};
};

CliOptions parseCommandLine(int argc, char* argv[]) {
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--mock") {
            opts.use_mock = true;
        } else if (arg == "--native") {
            opts.use_sandbox = false;
        } else if (arg == "--url" && i + 1 < argc) {
            opts.base_url = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            opts.model_name = argv[++i];
        } else if (arg == "--tasks" && i + 1 < argc) {
            opts.tasks_file = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            opts.output_dir = argv[++i];
        } else if (arg == "--prompt" && i + 1 < argc) {
            opts.single_prompt = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            // [KỸ THUẬT C++20]: std::format định dạng chuỗi hướng dẫn CLI
            printBannerLine(std::format(
                "Usage: {} [options]\n"
                "Options:\n"
                "  --url <URL>         Ollama server URL (default: http://localhost:11434)\n"
                "  --model <NAME>      Model name (default: qwen3-vl:7b or gemma4)\n"
                "  --tasks <PATH>      Path to benchmark tasks.json (default: benchmark/tasks.json)\n"
                "  --out <DIR>         Output directory for trajectories (default: trajectories)\n"
                "  --prompt <TEXT>     Run a single interactive task instead of batch benchmark\n"
                "  --mock              Use built-in deterministic MockLLMClient for offline self-test\n"
                "  --native            Run in current directory instead of SandboxEnvironment",
                argv[0]));
            std::exit(0);
        }
    }
    return opts;
}

} // namespace

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    // Thiết lập Console CodePage sang UTF-8 để hiển thị và đọc chuỗi tiếng Việt chuẩn xác
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    const CliOptions opts = parseCommandLine(argc, argv);

    // 1. Khởi tạo tầng LLMClient (đa hình qua std::shared_ptr<LLMClient>)
    std::shared_ptr<agent::client::LLMClient> llm_client;
    if (opts.use_mock) {
        printBannerLine("[INFO] Running in OFFLINE MOCK MODE (MockScriptedLLMClient).");
        llm_client = std::make_shared<MockScriptedLLMClient>();
    } else {
        agent::client::LLMConfig cfg;
        cfg.base_url = opts.base_url;
        cfg.model_name = opts.model_name;
        cfg.temperature = 0.1;
        cfg.max_tokens = 2048;
        cfg.timeout_seconds = 90;

        printBannerLine(std::format(
            "[INFO] Connecting to Ollama Server at {} with model '{}'",
            cfg.base_url, cfg.model_name));
        llm_client = std::make_shared<agent::client::OllamaClient>(cfg);
    }

    // 2. Khởi tạo tầng ToolRegistry (Registry/Factory Pattern) & đăng ký 5+3 Tools
    auto tool_registry = std::make_shared<agent::tools::ToolRegistry>();
    tool_registry->registerBuiltinTools("agent_memory.db");

    // 3. Khởi tạo tầng SkillLoader & AgentLoop (Template Method Pattern)
    agent::core::SkillLoader skill_loader("skills");
    const size_t loaded_skills = skill_loader.loadAllSkills();
    printBannerLine(std::format("[INFO] Loaded {} Markdown skills from 'skills/' directory.", loaded_skills));

    auto agent_loop = std::make_shared<agent::core::AgentLoop>(
        llm_client,
        tool_registry,
        std::move(skill_loader),
        10);

    // Helper tạo Environment (SandboxEnvironment vs NativeEnvironment)
    auto createEnvironment = [&opts]() -> std::unique_ptr<agent::harness::Environment> {
        return opts.use_sandbox
            ? std::unique_ptr<agent::harness::Environment>(
                  std::make_unique<agent::harness::SandboxEnvironment>("sandbox_workspace", false))
            : std::unique_ptr<agent::harness::Environment>(
                  std::make_unique<agent::harness::NativeEnvironment>());
    };

    // 4. Nếu người dùng truyền --prompt "..." thì chạy 1 task đơn lẻ
    if (!opts.single_prompt.empty()) {
        agent::harness::TaskSpec custom_task;
        custom_task.id = "interactive_task";
        custom_task.description = "Single interactive prompt from CLI";
        custom_task.instruction = opts.single_prompt;
        custom_task.eval_type = "keyword";
        custom_task.max_steps = 10;

        agent::harness::HarnessRunner runner(
            agent_loop,
            createEnvironment(),
            llm_client->getConfig().model_name);

        runner.registerEvaluator("vlm", std::make_shared<agent::harness::VLMEvaluator>(llm_client));

        const auto traj = runner.runSingleTask(custom_task, opts.output_dir);
        printBannerLine(std::format(
            "\n=== INTERACTIVE RUN FINISHED ===\n"
            "Final Answer : {}\n"
            "Steps Taken  : {}\n"
            "Total Tokens : {}\n"
            "Latency (ms) : {}\n"
            "Trajectory   : {}/trajectory_{}.json",
            traj.final_answer,
            traj.steps.size(),
            traj.total_tokens,
            traj.total_time_ms,
            opts.output_dir,
            traj.task_id));
        return traj.success ? 0 : 1;
    }

    // 5. Chạy Batch Evaluation trên toàn bộ tập benchmark/tasks.json
    const std::filesystem::path abs_tasks_path = std::filesystem::absolute(opts.tasks_file);

    agent::harness::HarnessRunner runner(
        agent_loop,
        createEnvironment(),
        llm_client->getConfig().model_name);

    // Đăng ký thêm VLMEvaluator vào HarnessRunner (bên cạnh FunctionalEvaluator và KeywordEvaluator)
    runner.registerEvaluator("vlm", std::make_shared<agent::harness::VLMEvaluator>(llm_client));

    const auto summary = runner.runBatchEvaluation(abs_tasks_path, opts.output_dir);
    printBannerLine(std::format(
        "[INFO] Detailed trajectories and benchmark_summary.json saved to '{}/'.",
        opts.output_dir));

    return (summary.total_tasks > 0 && summary.passed_tasks == summary.total_tasks) ? 0 : 1;
}