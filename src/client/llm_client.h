#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace agent::client {

/**
 * @brief Phân loại các lỗi có thể xảy ra khi gọi LLM API
 */
enum class LLMErrorCode {
    None,
    Timeout,            // Quá thời gian chờ phản hồi
    ConnectionRefused,  // Không kết nối được tới server (Ollama chưa bật / sai port)
    HttpError,          // HTTP status code != 200
    MalformedJson,      // Phản hồi từ server không đúng chuẩn JSON
    FileReadError,      // Lỗi đọc file ảnh cho Multimodal
    Unknown
};

/**
 * @brief Cấu trúc chi tiết lỗi trả về trong std::expected (C++23)
 */
struct LLMError {
    LLMErrorCode code{LLMErrorCode::None};
    std::string message;
};

/**
 * @brief Cấu hình kết nối và tham số sinh văn bản cho LLM
 */
struct LLMConfig {
    std::string base_url{"http://localhost:11434"};
    std::string model_name{"qwen3-vl:7b"};
    double temperature{0.2};
    int max_tokens{2048};
    long timeout_seconds{60};
};

/**
 * @brief Cấu trúc một tin nhắn trong hội thoại, hỗ trợ cả Text và Multimodal (ảnh Base64)
 */
struct Message {
    std::string role;                       // "system", "user", "assistant"
    std::string content;                    // Nội dung văn bản
    std::vector<std::string> images_base64; // Danh sách ảnh mã hóa Base64 (Multimodal)
};

/**
 * @brief Kết quả phản hồi từ LLM kèm thông số đo lường cho Harness
 */
struct LLMResponse {
    std::string content;
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
    long latency_ms{0};
};

/**
 * @brief Abstract Interface cho mọi LLM Client (Ollama, OpenAI, v.v.)
 * Đảm bảo Liskov Substitution Principle (LSP) và tách biệt tầng Agent khỏi API cụ thể.
 */
class LLMClient {
public:
    LLMClient() = default;
    virtual ~LLMClient() = default;

    // [KỸ THUẬT C++26 - P2573R2]: = delete("reason") giải thích lý do cấm copy tại compile-time
#if defined(__cpp_deleted_function) && __cpp_deleted_function >= 202403L
    LLMClient(const LLMClient&) = delete("LLMClient manages network session state and cannot be copied. Use std::shared_ptr or std::unique_ptr.");
    LLMClient& operator=(const LLMClient&) = delete("LLMClient manages network session state and cannot be copied.");
#else
    LLMClient(const LLMClient&) = delete;
    LLMClient& operator=(const LLMClient&) = delete;
#endif

    LLMClient(LLMClient&&) noexcept = default;
    LLMClient& operator=(LLMClient&&) noexcept = default;

    /**
     * @brief Gửi danh sách tin nhắn (hỗ trợ cả text-only và multimodal) tới LLM
     * @return std::expected<LLMResponse, LLMError> (Kỹ thuật C++23)
     */
    [[nodiscard]] virtual std::expected<LLMResponse, LLMError> chat(
        const std::vector<Message>& messages) = 0;

    /**
     * @brief Cập nhật và truy xuất cấu hình client
     */
    virtual void setConfig(const LLMConfig& config) = 0;
    [[nodiscard]] virtual const LLMConfig& getConfig() const noexcept = 0;

    /**
     * @brief Tiện ích đọc file ảnh và mã hóa sang Base64 để gửi qua Multimodal interface
     * Sử dụng std::filesystem (C++17) và std::span (C++20)
     */
    [[nodiscard]] static std::expected<std::string, LLMError> encodeImageFileToBase64(
        const std::filesystem::path& image_path);

    /**
     * @brief Mã hóa một vùng nhớ byte bất kỳ sang chuỗi Base64 (dùng std::span C++20)
     */
    [[nodiscard]] static std::string encodeBase64(std::span<const unsigned char> data);
};

} // namespace agent::client