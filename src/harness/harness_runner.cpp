#include "harness/harness_runner.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace agent::harness {

// ==============================================================================
// 1. Triển khai NativeEnvironment & SandboxEnvironment
// ==============================================================================

NativeEnvironment::NativeEnvironment(std::filesystem::path work_dir)
    : work_dir_(std::move(work_dir)) {}

bool NativeEnvironment::setup() {
    std::error_code ec;
    original_dir_ = std::filesystem::current_path(ec);
    if (!std::filesystem::exists(work_dir_, ec)) {
        std::filesystem::create_directories(work_dir_, ec);
    }
    std::filesystem::current_path(work_dir_, ec);
    return !ec;
}

void NativeEnvironment::teardown() {
    std::error_code ec;
    if (!original_dir_.empty()) {
        std::filesystem::current_path(original_dir_, ec);
    }
}

SandboxEnvironment::SandboxEnvironment(std::filesystem::path sandbox_root,
                                       bool cleanup_on_teardown)
    : sandbox_dir_(std::move(sandbox_root)),
      cleanup_on_teardown_(cleanup_on_teardown) {}

bool SandboxEnvironment::setup() {
    std::error_code ec;
    original_dir_ = std::filesystem::current_path(ec);
    std::filesystem::create_directories(sandbox_dir_, ec);
    if (ec) {
        return false;
    }

    // Dọn dẹp các artifact từ lần chạy trước trong sandbox_workspace (trừ thư mục skills)
    // để đảm bảo tính độc lập giữa các Task và tránh hiện tượng false positive trong Evaluator
    for (const auto& entry : std::filesystem::directory_iterator(sandbox_dir_, ec)) {
        if (entry.path().filename() != "skills") {
            std::filesystem::remove_all(entry.path(), ec);
        }
    }

    // Nếu thư mục skills tồn tại ở thư mục gốc, copy sang sandbox để SkillLoader đọc được
    const auto orig_skills = original_dir_ / "skills";
    const auto dest_skills = sandbox_dir_ / "skills";
    if (std::filesystem::exists(orig_skills, ec) && !std::filesystem::exists(dest_skills, ec)) {
        std::filesystem::copy(orig_skills, dest_skills,
                              std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                              ec);
    }

    std::filesystem::current_path(sandbox_dir_, ec);
    return !ec;
}

void SandboxEnvironment::teardown() {
    std::error_code ec;
    if (!original_dir_.empty()) {
        std::filesystem::current_path(original_dir_, ec);
    }
    if (cleanup_on_teardown_ && std::filesystem::exists(sandbox_dir_, ec)) {
        std::filesystem::remove_all(sandbox_dir_, ec);
    }
}

// ==============================================================================
// 2. Triển khai HarnessRunner
// ==============================================================================

HarnessRunner::HarnessRunner(std::shared_ptr<core::AgentLoop> agent_loop,
                             std::unique_ptr<Environment> environment,
                             std::string model_name)
    : agent_loop_(std::move(agent_loop)),
      env_(std::move(environment)),
      model_name_(std::move(model_name)) {
    // Đăng ký sẵn 2 Evaluator bắt buộc theo mặc định
    registerEvaluator("functional", std::make_shared<FunctionalEvaluator>());
    registerEvaluator("keyword", std::make_shared<KeywordEvaluator>());
}

void HarnessRunner::registerEvaluator(const std::string& eval_type,
                                      std::shared_ptr<Evaluator> evaluator) {
    if (!eval_type.empty() && evaluator) {
        evaluators_[eval_type] = std::move(evaluator);
    }
}

std::vector<TaskSpec> HarnessRunner::loadTasksFromJson(
    const std::filesystem::path& json_path) {
    
    std::vector<TaskSpec> tasks;
    std::ifstream in(json_path);
    if (!in.is_open()) {
        std::cerr << "[HarnessRunner] Cannot open tasks file: " << json_path << "\n";
        return tasks;
    }

    try {
        const auto root = nlohmann::json::parse(in);
        if (!root.is_array()) {
            return tasks;
        }

        for (const auto& item : root) {
            TaskSpec spec;
            spec.id = item.value("id", "unknown_task");
            spec.description = item.value("description", "");
            spec.instruction = item.value("instruction", "");
            spec.eval_type = item.value("eval_type", "functional");
            spec.eval_script = item.value("eval_script", "");
            spec.max_steps = item.value("max_steps", 10);

            if (item.contains("expected_keywords") && item["expected_keywords"].is_array()) {
                for (const auto& kw : item["expected_keywords"]) {
                    if (kw.is_string()) {
                        spec.expected_keywords.push_back(kw.get<std::string>());
                    }
                }
            }
            tasks.push_back(std::move(spec));
        }
    } catch (const std::exception& e) {
        std::cerr << "[HarnessRunner] Failed to parse tasks.json: " << e.what() << "\n";
    }

    return tasks;
}

Trajectory HarnessRunner::runSingleTask(
    const TaskSpec& task,
    const std::filesystem::path& output_dir) {
    
    Trajectory traj(task.id, model_name_);
    if (!agent_loop_) {
        traj.eval_reason = "AgentLoop instance is null.";
        return traj;
    }

    // Lưu lại đường dẫn tuyệt đối của thư mục xuất kết quả trước khi Environment đổi thư mục
    std::error_code ec;
    const std::filesystem::path abs_output_dir = std::filesystem::absolute(output_dir, ec);

    // 1. Setup Environment
    if (env_ && !env_->setup()) {
        traj.eval_reason = "Environment setup failed.";
        return traj;
    }

    // [KỸ THUẬT C++26 - P2169R4]: Dùng biến placeholder '_' làm RAII Scope Guard
    // để đảm bảo luôn gỡ step_hook và gọi env_->teardown() khi kết thúc hàm
    struct ScopeCleanup {
        core::AgentLoop* loop;
        Environment* env;
        ~ScopeCleanup() {
            if (loop) loop->setStepHook(nullptr);
            if (env) env->teardown();
        }
    } _{agent_loop_.get(), env_.get()};

    // 2. [OBSERVER / HOOK PATTERN]: Inject Lambda step_hook vào AgentLoop để ghi nhận Trajectory
    agent_loop_->setMaxSteps(task.max_steps);
    agent_loop_->setStepHook([&traj](const core::StepEvent& ev) {
        Step step_record{
            ev.step_id,
            ev.thought,
            ev.action_type,
            ev.tool_name,
            ev.tool_args,
            ev.tool_result,
            ev.tokens_used,
            ev.latency_ms
        };
        traj.addStep(std::move(step_record));
    });

    // 3. Chạy AgentLoop
    const core::AgentRunResult run_res = agent_loop_->run(task.instruction);
    traj.total_tokens = run_res.total_tokens;
    traj.total_time_ms = run_res.total_time_ms;
    traj.final_answer = run_res.final_answer;

    // 4. [STRATEGY PATTERN]: Kiểm tra tính hợp lệ và chọn Evaluator để đánh giá kết quả
    // Nếu Agent gặp lỗi (LLM error, timeout, vòng lặp dừng bất thường) và không hoàn thành,
    // đánh dấu FAIL ngay lập tức để tránh False Positive từ các file rác.
    if (!run_res.completed) {
        traj.success = false;
        traj.eval_score = 0.0;
        traj.eval_reason = "Agent execution did not complete: " + run_res.final_answer;
    } else {
        auto eval_it = evaluators_.find(task.eval_type);
        if (eval_it == evaluators_.end()) {
            eval_it = evaluators_.find("functional");
        }

        if (eval_it != evaluators_.end() && eval_it->second) {
            const EvalResult eval_res = eval_it->second->evaluate(task, run_res.final_answer);
            traj.success = eval_res.passed;
            traj.eval_score = eval_res.partial_score;
            traj.eval_reason = eval_res.reason;
        } else {
            traj.success = run_res.completed;
            traj.eval_reason = "No matching evaluator found; defaulted to AgentLoop completion state.";
        }
    }

    // 5. Xuất file trajectory_{task_id}.json
    const std::filesystem::path traj_file = abs_output_dir / ("trajectory_" + task.id + ".json");
    (void)traj.saveToFile(traj_file);

    return traj;
}

BatchEvalSummary HarnessRunner::runBatchEvaluation(
    const std::filesystem::path& tasks_json_path,
    const std::filesystem::path& output_dir) {
    
    BatchEvalSummary summary{};
    const auto tasks = loadTasksFromJson(tasks_json_path);
    summary.total_tasks = tasks.size();

    if (tasks.empty()) {
        return summary;
    }

    std::error_code ec;
    const std::filesystem::path abs_output_dir = std::filesystem::absolute(output_dir, ec);
    std::filesystem::create_directories(abs_output_dir, ec);

    std::cout << "============================================================\n"
              << " STARTING HARNESS BATCH EVALUATION (" << tasks.size() << " tasks)\n"
              << " Model: " << model_name_ << "\n"
              << "============================================================\n";

    for (const auto& task : tasks) {
        std::cout << "[RUN ] " << task.id << " - " << task.description << " ... " << std::flush;
        Trajectory traj = runSingleTask(task, abs_output_dir);

        if (traj.success) {
            ++summary.passed_tasks;
            std::cout << "PASS (" << traj.total_time_ms << " ms, "
                      << traj.total_tokens << " tokens)\n";
        } else {
            std::cout << "FAIL (" << traj.eval_reason << ")\n";
        }

        summary.trajectories.push_back(std::move(traj));
    }

    summary.success_rate = (summary.total_tasks > 0)
                               ? (static_cast<double>(summary.passed_tasks) * 100.0 /
                                  static_cast<double>(summary.total_tasks))
                               : 0.0;

    // Xuất file tổng hợp kết quả benchmark_summary.json
    nlohmann::json report_json;
    report_json["model"] = model_name_;
    report_json["total_tasks"] = summary.total_tasks;
    report_json["passed_tasks"] = summary.passed_tasks;
    report_json["success_rate_percent"] = summary.success_rate;

    nlohmann::json task_results = nlohmann::json::array();
    for (const auto& t : summary.trajectories) {
        task_results.push_back({
            {"task_id", t.task_id},
            {"success", t.success},
            {"score", t.eval_score.value_or(t.success ? 1.0 : 0.0)},
            {"total_tokens", t.total_tokens},
            {"total_time_ms", t.total_time_ms},
            {"eval_reason", t.eval_reason}
        });
    }
    report_json["results"] = std::move(task_results);

    std::ofstream summary_out(abs_output_dir / "benchmark_summary.json");
    if (summary_out.is_open()) {
        summary_out << report_json.dump(2) << "\n";
    }

    std::cout << "============================================================\n"
              << " BATCH EVALUATION COMPLETED: " << summary.passed_tasks << "/"
              << summary.total_tasks << " PASSED ("
              << std::fixed << std::setprecision(1) << summary.success_rate << "%)\n"
              << "============================================================\n";

    return summary;
}

} // namespace agent::harness