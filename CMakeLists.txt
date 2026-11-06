cmake_minimum_required(VERSION 3.25)

project(OllamaAIAgent
    VERSION 1.0.0
    DESCRIPTION "C++ OOP AI Agent Framework with Ollama API"
    LANGUAGES CXX
)

# ==============================================================================
# 1. THIẾT LẬP CHUẨN C++26 (Hỗ trợ C++17, C++20, C++23, C++26)
# ==============================================================================
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Bật cảnh báo nghiêm ngặt để đảm bảo chất lượng code và tránh memory leak
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ==============================================================================
# 2. TÌM KIẾM VÀ LIÊN KẾT THƯ VIỆN BÊN THỨ BA
# ==============================================================================
# 2.1. Thư viện đa luồng (Threads)
find_package(Threads REQUIRED)

# 2.2. Thư viện libcurl (Gửi HTTP request cho OllamaClient & WebSearchTool)
find_package(CURL REQUIRED)

# 2.3. Thư viện SQLite3 (Lưu trữ dữ liệu cho MemoryTool)
find_package(SQLite3 REQUIRED)

# 2.4. Thư viện nlohmann/json (Xử lý JSON cho API, Tool Call và Trajectory)
find_package(nlohmann_json 3.11.0 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json không có sẵn trên hệ thống. Đang tải qua FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(json)
endif()

# ==============================================================================
# 3. CẤU HÌNH INCLUDE DIRECTORIES & SOURCE FILES
# ==============================================================================
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Gom các file mã nguồn của từng tầng thành 1 thư viện core (agent_core)
# Giúp tái sử dụng code giữa chương trình chạy chính và các file unit test
file(GLOB_RECURSE AGENT_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/agent/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/harness/*.cpp"
)

# Ở Phase 1 khi chưa có file .cpp nào trong src/, tạo một biến kiểm tra
if(AGENT_CORE_SOURCES)
    add_library(agent_core STATIC ${AGENT_CORE_SOURCES})
    target_link_libraries(agent_core
        PUBLIC
            CURL::libcurl
            SQLite::SQLite3
            nlohmann_json::nlohmann_json
            Threads::Threads
    )
endif()

# ==============================================================================
# 4. TARGET THỰC THI (BENCHMARK / MAIN)
# ==============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/run_eval.cpp")
    add_executable(run_eval benchmark/run_eval.cpp)
    target_link_libraries(run_eval PRIVATE agent_core)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_agent.cpp")
    add_executable(test_agent tests/test_agent.cpp)
    target_link_libraries(test_agent PRIVATE agent_core)
endif()

message(STATUS "=======================================================")
message(STATUS "Cấu hình thành công dự án: ${PROJECT_NAME}")
message(STATUS "Trình biên dịch C++      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Chuẩn C++ thiết lập      : C++${CMAKE_CXX_STANDARD}")
message(STATUS "=======================================================")