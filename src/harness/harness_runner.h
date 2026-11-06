#pragma once

#include "agent/agent_loop.h"
#include "harness/evaluator.h"
#include "harness/trajectory.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent::harness {

// ==============================================================================
// 1. ENVIRONMENT HIERARCHY (MỤC 4.1 CLASS DIAGRAM)
// ==============================================================================

/**
 * @brief Abstract Interface quản lý môi trường thực thi của Agent
 */
class Environment {
public:
    Environment() = default;
    virtual ~Environment() = default;

    /**
     * @brief Chuẩn bị môi trường trước khi chạy Task
     */
    [[nodiscard]] virtual bool setup() = 0;

    /**
     * @brief Dọn dẹp môi trường sau khi chạy và chấm điểm xong
     */
    virtual void teardown() = 0;

    /**
     * @brief Đường dẫn thư mục làm việc của môi trường
     */
    [[nodiscard]] virtual std::filesystem::path workingDirectory() const = 0;
};

/**
 * @brief Môi trường chạy trực tiếp trên thư mục hiện tại của hệ thống
 */
class NativeEnvironment final : public Environment {
public:
    explicit NativeEnvironment(std::filesystem::path work_dir = std::filesystem::current_path());

    [[nodiscard]] bool setup() override;
    void teardown() override;
    [[nodiscard]] std::filesystem::path workingDirectory() const override { return work_dir_; }

private:
    std::filesystem::path work_dir_;
    std::filesystem::path original_dir_;
};

/**
 * @brief Môi trường cô lập (Sandbox) tạo thư mục tạm riêng biệt cho từng lần chạy
 */
class SandboxEnvironment final : public Environment {
public:
    explicit SandboxEnvironment(std::filesystem::path sandbox_root = "sandbox_workspace",
                                bool cleanup_on_teardown = false);

    [[nodiscard]] bool setup() override;
    void teardown() override;
    [[nodiscard]] std::filesystem::path workingDirectory() const override { return sandbox_dir_; }

private:
    std::filesystem::path sandbox_dir_;
    std::filesystem::path original_dir_;
    bool cleanup_on_teardown_{false};
};

// ==============================================================================
// 2. HARNESS RUNNER (OBSERVER / HOOK HOST & BATCH ORCHESTRATOR)
// ==============================================================================

/**
 * @brief Thống kê kết quả sau khi chạy Batch Evaluation trên tập Benchmark
 */
struct BatchEvalSummary {
    size_t total_tasks{0};
    size_t passed_tasks{0};
    double success_rate{0.0};
    std::vector<Trajectory> trajectories;
};

/**
 * @brief Lớp điều phối toàn bộ quy trình:
 * Setup Environment -> Inject Hook -> Run Agent -> Evaluate -> Record Trajectory.
 */
class HarnessRunner {
public:
    HarnessRunner(std::shared_ptr<core::AgentLoop> agent_loop,
                  std::unique_ptr<Environment> environment,
                  std::string model_name = "qwen3-vl:7b");

    ~HarnessRunner() = default;

    // [KỸ THUẬT C++26 - P2573R2]: = delete("reason") cấm sao chép đối tượng HarnessRunner
#if defined(__cpp_deleted_function) && __cpp_deleted_function >= 202403L
    HarnessRunner(const HarnessRunner&) = delete("HarnessRunner owns unique Environment resources and cannot be copied.");
    HarnessRunner& operator=(const HarnessRunner&) = delete("HarnessRunner cannot be copied.");
#else
    HarnessRunner(const HarnessRunner&) = delete;
    HarnessRunner& operator=(const HarnessRunner&) = delete;
#endif

    /**
     * @brief Đăng ký chiến lược Evaluator theo tên ("functional", "keyword", "vlm")
     */
    void registerEvaluator(const std::string& eval_type,
                           std::shared_ptr<Evaluator> evaluator);

    /**
     * @brief Đọc danh sách Task từ file JSON (ví dụ: benchmark/tasks.json)
     */
    [[nodiscard]] static std::vector<TaskSpec> loadTasksFromJson(
        const std::filesystem::path& json_path);

    /**
     * @brief Chạy một Task đơn lẻ, thu thập Trajectory qua Hook và chấm điểm bằng Evaluator
     */
    [[nodiscard]] Trajectory runSingleTask(
        const TaskSpec& task,
        const std::filesystem::path& output_dir = "trajectories");

    /**
     * @brief Chạy toàn bộ tập Task (Batch Evaluation), tính Success Rate và xuất JSON tổng kết
     */
    [[nodiscard]] BatchEvalSummary runBatchEvaluation(
        const std::filesystem::path& tasks_json_path,
        const std::filesystem::path& output_dir = "trajectories");

private:
    std::shared_ptr<core::AgentLoop> agent_loop_;
    std::unique_ptr<Environment> env_;
    std::string model_name_;
    std::unordered_map<std::string, std::shared_ptr<Evaluator>> evaluators_;
};

} // namespace agent::harness