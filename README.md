# C++ OOP AI Agent Framework with Ollama API

Dự án đồ án xây dựng hệ thống **Autonomous ReAct AI Agent** bằng ngôn ngữ **C++ hiện đại (C++17/20/23/26)** tích hợp **Ollama API** (`/api/chat`).

Hệ thống được thiết kế hướng đối tượng (OOP) theo chuẩn công nghiệp, tuân thủ nghiêm ngặt nguyên tắc phân tách tầng độc lập (**Abstraction Layers**) và áp dụng **4 Design Patterns** kinh điển.

---

## 1. Kiến Trúc 4 Tầng & 4 Design Patterns

```
+-------------------------------------------------------------+
|                      HARNESS LAYER                          |
|   HarnessRunner (Observer Host)  |  Evaluator (Strategy)    |
|   Trajectory & Step              |  Environment (Sandbox)   |
+-------------------------------------------------------------+
                              |
                     (step_hook Observer)
                              v
+-------------------------------------------------------------+
|                       AGENT LAYER                           |
|   AgentLoop (Template Method: Observe -> Think -> Act)      |
|   LoopDetector (Repeat / Ping-Pong) | SkillLoader (MD)     |
+-------------------------------------------------------------+
               |                               |
       (Abstract Interface)           (Registry / Factory)
               v                               v
+-----------------------------+ +-----------------------------+
|        CLIENT LAYER         | |         TOOLS LAYER         |
|   LLMClient (Abstract)      | |   ToolRegistry (Factory)    |
|   OllamaClient (libcurl)    | |   5 Concrete Tools          |
|   MockScriptedLLMClient     | |   3 Lambda Dynamic Tools    |
+-----------------------------+ +-----------------------------+
```

### 1.1. Bốn Design Patterns Cốt Lõi
1. **Strategy Pattern** (`src/harness/evaluator.h`):
   - Phân tách chiến lược chấm điểm độc lập khỏi quá trình chạy Agent.
   - Các concrete strategies: `FunctionalEvaluator` (chạy script kiểm tra), `KeywordEvaluator` (đối sánh từ khóa), `VLMEvaluator` (LLM-as-a-Judge).
2. **Template Method Pattern** (`src/agent/agent_loop.h`):
   - Khung xương của vòng lặp ReAct: `observe()` $\rightarrow$ `think()` $\rightarrow$ `parseLLMOutput()` $\rightarrow$ `act()`.
3. **Registry / Factory Pattern** (`src/tools/tool_registry.h`):
   - Đăng ký và tạo động các Tool (`ExecTool`, `FileTool`, `CalculatorTool`, `MemoryTool`, `WebSearchTool`, cùng các Lambda Tool).
   - Kiểm soát quyền thực thi thông qua **Security Policy** (`allow_list`, `deny_list`).
4. **Observer / Hook Pattern** (`src/agent/agent_loop.h` & `src/harness/harness_runner.h`):
   - `AgentLoop` phát `StepEvent` qua `step_hook` mà hoàn toàn **không phụ thuộc** vào `HarnessRunner`.

---

## 2. Các Kỹ Thuật C++ Hiện Đại Được Ứng Dụng

| Chuẩn C++ | Kỹ Thuật Áp Dụng | Vị Trí Trong Mã Nguồn | Mục Đích |
| :--- | :--- | :--- | :--- |
| **C++17** | `std::unique_ptr`, `std::shared_ptr` | Toàn bộ dự án | Quản lý bộ nhớ RAII an toàn, chống rò rỉ tài nguyên |
| **C++17** | `std::optional<T>` | `ToolResult`, `EvalResult` | Đại diện kết quả có thể rỗng hoặc lỗi |
| **C++17** | `std::variant` + `std::visit` | `AgentAction`, `AgentLoop::act()` | Đa hình phi kế thừa hiệu năng cao cho các hành động |
| **C++17** | `std::filesystem` | `FileTool`, `SkillLoader`, `Harness` | Thao tác file, kiểm tra thư mục đa nền tảng |
| **C++17** | `Structured Bindings` | `OllamaClient`, `ToolRegistry` | Unpack tuple/struct/map dễ đọc |
| **C++20** | `Concepts & requires` | `DerivedFromTool` (`tool_registry.h`) | Ràng buộc kiểu dữ liệu tại compile-time cho template |
| **C++20** | `std::span` | `LLMClient::encodeBase64` | Truyền view mảng byte hiệu năng cao, zero-copy |
| **C++20** | `std::format` / `ends_with` | `run_eval.cpp`, `ollama_client.cpp` | Định dạng chuỗi CLI và chuẩn hóa URL |
| **C++23** | `std::expected<T, E>` | `LLMClient::chat()`, `ImageEncoder` | Xử lý lỗi tường minh, không lạm dụng exception |
| **C++23** | `std::string::contains` | `loop_detector`, `skill_loader`, `run_eval` | Kiểm tra chuỗi con trực quan, tối ưu |
| **C++26** | `Placeholder variables (_)` | `ollama_client.cpp`, `memory_tool.cpp`, `harness_runner.cpp` | Định danh biến vô danh tái sử dụng cho RAII Scope Guard |
| **C++26** | `= delete("reason")` | `LLMClient`, `AgentLoop`, `HarnessRunner` | Cung cấp thông báo giải thích cấm copy tại compile-time |

---

## 3. Cấu Trúc Thư Mục Dự Án

```
Test OOP/
├── CMakeLists.txt              # Cấu hình build chuẩn C++26, tìm curl, sqlite3, nlohmann_json
├── README.md                   # Tài liệu hướng dẫn đồ án
├── benchmark/
│   ├── run_eval.cpp            # Điểm khởi chạy CLI (hỗ trợ cả --mock và Ollama server)
│   └── tasks.json              # 10 tasks kiểm thử đa cấp độ (Đơn giản, Trung bình, Khó)
├── docs/
│   ├── class_diagram.md        # Sơ đồ kiến trúc tổng quan & UML Class Diagram (Mermaid)
│   ├── sequence_diagram_agent.md   # Sequence Diagram chi tiết ReAct Loop
│   └── sequence_diagram_harness.md # Sequence Diagram điều phối Harness & Benchmark
├── skills/
│   ├── error_recovery.md       # Kỹ năng xử lý lỗi và chống lặp
│   ├── file_and_data_ops.md    # Kỹ năng thao tác file và JSON
│   └── task_planner.md         # Kỹ năng phân rã bài toán nhiều bước
├── src/
│   ├── agent/                  # Tầng AgentLoop, LoopDetector, SkillLoader
│   ├── client/                 # Tầng LLMClient, OllamaClient (Multimodal & Text)
│   ├── harness/                # Tầng HarnessRunner, Evaluators, Trajectory, Environment
│   └── tools/                  # Tầng ToolRegistry và 5+3 Tools
└── tests/
    └── test_agent.cpp          # Bộ Unit Tests độc lập kiểm tra toàn diện core logic
```

---

## 4. Hướng Dẫn Biên Dịch & Chạy Chương Trình

### 4.1. Yêu Cầu Môi Trường
- **Trình biên dịch C++**: GCC 14+ / Clang 18+ (hỗ trợ C++26 / C++2c).
- **Thư viện bên thứ ba**:
  - `libcurl` (HTTP requests cho Ollama và WebSearchTool)
  - `sqlite3` (Lưu trữ bộ nhớ cho MemoryTool)
  - `nlohmann/json` (Tự động tải qua FetchContent nếu hệ thống chưa cài)

### 4.2. Lệnh Cài Đặt Thư Viện
- **Trên Ubuntu / WSL / Debian**:
  ```bash
  sudo apt-get update
  sudo apt-get install -y build-essential cmake ninja-build libcurl4-openssl-dev libsqlite3-dev
  ```
- **Trên Windows (MSYS2 UCRT64)**:
  ```bash
  pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-sqlite3 mingw-w64-ucrt-x86_64-nlohmann-json
  ```

### 4.3. Các Bước Build Bằng CMake & Ninja
```bash
# 1. Cấu hình CMake
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# 2. Biên dịch toàn bộ
cmake --build build
```

---

## 5. Kiểm Thử Hệ Thống (Testing & Evaluation)

### 5.1. Chạy Bộ Unit Tests
Kiểm tra Calculator, ToolRegistry Security Policy, LoopDetector, SkillLoader và Trajectory:
```bash
./build/test_agent
```
Kết quả kỳ vọng: **5/5 tests PASSED**.

### 5.2. Chạy Benchmark Chế Độ Tự Động Offline (--mock)
Sử dụng `MockScriptedLLMClient` để tự động hóa toàn bộ 10 tasks mà không phụ thuộc Ollama:
```bash
./build/run_eval --mock
```
Kết quả kỳ vọng: **10/10 tasks PASSED (100.0%)**, xuất file `trajectories/benchmark_summary.json` và các file `trajectory_task_*.json`.

### 5.3. Chạy Với Ollama Server Thực Tế
Đảm bảo đã khởi động Ollama (`ollama serve`) và kéo model:
```bash
ollama run qwen3-vl:7b
```
Chạy đánh giá benchmark trên mô hình:
```bash
./build/run_eval --url http://localhost:11434 --model qwen3-vl:7b --tasks benchmark/tasks.json --out trajectories
```

### 5.4. Chạy Một Prompt Tương Tác Đơn Lẻ
```bash
./build/run_eval --prompt "Tính 15 * 17 và lưu vào result.txt"
```
