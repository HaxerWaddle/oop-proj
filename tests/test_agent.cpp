#include "agent/loop_detector.h"
#include "agent/skill_loader.h"
#include "harness/trajectory.h"
#include "tools/tool.h"
#include "tools/tool_registry.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

void testCalculatorTool() {
    std::cout << "[TEST] Running CalculatorTool tests... " << std::flush;
    agent::tools::CalculatorTool calc;

    // 1. Phép tính cơ bản
    auto res1 = calc.execute("15*17");
    assert(res1.has_value() && res1->success && res1->output == "255");

    // 2. Biểu thức có độ ưu tiên ngoặc và lũy thừa
    auto res2 = calc.execute("(125 + 375) / 25 + 2^5");
    assert(res2.has_value() && res2->success && res2->output == "52");

    // 3. Xử lý chia cho 0 an toàn (không crash)
    auto res3 = calc.execute("10 / 0");
    assert(res3.has_value() && !res3->success);

    std::cout << "PASSED\n";
}

void testToolRegistryAndPolicy() {
    std::cout << "[TEST] Running ToolRegistry & Security Policy tests... " << std::flush;
    agent::tools::ToolRegistry registry;
    registry.registerBuiltinTools("test_memory.db");

    // Kiểm tra có đầy đủ 5 tools chuẩn + 3 tools mở rộng
    assert(registry.getTool("exec") != nullptr);
    assert(registry.getTool("file") != nullptr);
    assert(registry.getTool("calculator") != nullptr);
    assert(registry.getTool("memory") != nullptr);
    assert(registry.getTool("web_search") != nullptr);
    assert(registry.getTool("datetime_info") != nullptr);
    assert(registry.getTool("system_inspect") != nullptr);
    assert(registry.getTool("json_query") != nullptr);

    // Kiểm tra Security Policy: deny_list
    registry.setPolicy({}, {"exec"});
    assert(!registry.isAllowed("exec"));
    assert(registry.isAllowed("calculator"));

    auto blocked_res = registry.executeTool("exec", "{\"command\": \"ls\"}");
    assert(blocked_res.has_value() && !blocked_res->success);

    // Kiểm tra Security Policy: allow_list
    registry.setPolicy({"calculator", "file"}, {});
    assert(registry.isAllowed("calculator"));
    assert(registry.isAllowed("file"));
    assert(!registry.isAllowed("datetime_info"));

    std::cout << "PASSED\n";
}

void testLoopDetector() {
    std::cout << "[TEST] Running LoopDetector (Repeat & Ping-Pong) tests... " << std::flush;
    agent::core::LoopDetector detector(2, 3);

    // 1. Generic Repeat Detection
    auto r1 = detector.recordAction("calculator", "1+1");
    assert(r1.status == agent::core::LoopStatus::None);

    auto r2 = detector.recordAction("calculator", "1+1");
    assert(r2.status == agent::core::LoopStatus::WarningRepeat);

    auto r3 = detector.recordAction("calculator", "1+1");
    assert(r3.status == agent::core::LoopStatus::CriticalRepeat);

    // 2. Ping-Pong Loop Detection
    detector.reset();
    (void)detector.recordAction("toolA", "argsA");
    (void)detector.recordAction("toolB", "argsB");
    (void)detector.recordAction("toolA", "argsA");
    auto p2 = detector.recordAction("toolB", "argsB");
    assert(p2.status == agent::core::LoopStatus::WarningPingPong);

    (void)detector.recordAction("toolA", "argsA");
    auto p3 = detector.recordAction("toolB", "argsB");
    assert(p3.status == agent::core::LoopStatus::CriticalPingPong);

    std::cout << "PASSED\n";
}

void testSkillLoader() {
    std::cout << "[TEST] Running SkillLoader tests... " << std::flush;
    agent::core::SkillLoader loader("skills");
    size_t count = loader.loadAllSkills();
    assert(count >= 3);

    // Khớp từ khóa task tính toán
    auto skills = loader.selectSkillsForTask("Tính 15 nhân 17 và lưu kết quả");
    assert(!skills.empty());

    // Prompt sinh ra có chứa định dạng skill
    std::string section = loader.buildSkillPromptSection("Tính 15 nhân 17");
    assert(!section.empty() && section.find("ACTIVATED AGENT SKILLS") != std::string::npos);

    std::cout << "PASSED\n";
}

void testTrajectorySerialization() {
    std::cout << "[TEST] Running Trajectory serialization tests... " << std::flush;
    agent::harness::Trajectory traj("unit_test_01", "mock_model");
    traj.success = true;
    traj.total_tokens = 150;
    traj.total_time_ms = 45;
    traj.final_answer = "All tests completed.";

    agent::harness::Step s1{};
    s1.step_id = 0;
    s1.thought = "Testing thought";
    s1.action_type = "tool_call";
    s1.tool_name = "calculator";
    s1.tool_args = "2+2";
    s1.tool_result = "4";
    traj.addStep(s1);

    std::string json_str = traj.toJson(2);
    assert(json_str.find("unit_test_01") != std::string::npos);
    assert(json_str.find("mock_model") != std::string::npos);
    assert(json_str.find("Testing thought") != std::string::npos);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << " RUNNING AI AGENT FRAMEWORK UNIT TESTS\n";
    std::cout << "========================================\n";

    testCalculatorTool();
    testToolRegistryAndPolicy();
    testLoopDetector();
    testSkillLoader();
    testTrajectorySerialization();

    std::cout << "========================================\n";
    std::cout << " ALL UNIT TESTS PASSED SUCCESSFULLY! (5/5)\n";
    std::cout << "========================================\n";
    return 0;
}
