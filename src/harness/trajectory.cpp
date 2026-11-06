#include "harness/trajectory.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace agent::harness {

Trajectory::Trajectory(std::string id, std::string model_name)
    : task_id(std::move(id)), model(std::move(model_name)) {}

void Trajectory::addStep(Step step) {
    steps.push_back(std::move(step));
}

std::string Trajectory::toJson(int indent) const {
    using json = nlohmann::json;

    json j;
    j["task_id"] = task_id;
    j["model"] = model;
    j["success"] = success;
    j["total_tokens"] = total_tokens;
    j["total_time_ms"] = total_time_ms;
    j["final_answer"] = final_answer;

    if (eval_score.has_value()) {
        j["eval_score"] = eval_score.value();
    }
    if (!eval_reason.empty()) {
        j["eval_reason"] = eval_reason;
    }

    json steps_array = json::array();
    // [KỸ THUẬT C++17]: Range-based for duyệt danh sách các bước trong Trajectory
    for (const auto& s : steps) {
        json action_obj;
        action_obj["type"] = s.action_type;
        if (s.action_type == "done") {
            action_obj["answer"] = s.tool_result;
        } else {
            action_obj["tool"] = s.tool_name;
            action_obj["args"] = s.tool_args;
        }

        steps_array.push_back({
            {"step_id", s.step_id},
            {"thought", s.thought},
            {"action", std::move(action_obj)},
            {"tool_result", s.tool_result},
            {"tokens_used", s.tokens_used},
            {"latency_ms", s.latency_ms}
        });
    }

    j["steps"] = std::move(steps_array);
    return j.dump(indent);
}

bool Trajectory::saveToFile(const std::filesystem::path& filepath) const {
    std::error_code ec;
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path(), ec);
    }

    std::ofstream out(filepath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << toJson(2) << "\n";
    return out.good();
}

} // namespace agent::harness