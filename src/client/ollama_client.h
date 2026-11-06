#pragma once

#include "client/llm_client.h"
#include <string_view>

namespace agent::client {

/**
 * @brief Lớp triển khai LLMClient kết nối tới Ollama REST API (/api/chat)
 */
class OllamaClient final : public LLMClient {
public:
    explicit OllamaClient(LLMConfig config = {});
    ~OllamaClient() override = default;

    /**
     * @brief Gửi HTTP POST tới /api/chat của Ollama, hỗ trợ cả text và ảnh base64
     */
    [[nodiscard]] std::expected<LLMResponse, LLMError> chat(
        const std::vector<Message>& messages) override;

    void setConfig(const LLMConfig& config) override;
    [[nodiscard]] const LLMConfig& getConfig() const noexcept override;

private:
    LLMConfig config_;

    /**
     * @brief Chuẩn hóa base_url (loại bỏ dấu '/' ở cuối bằng C++20 ends_with)
     */
    void normalizeBaseUrl();

    /**
     * @brief Thực hiện HTTP POST request thông qua libcurl với quản lý bộ nhớ RAII
     */
    [[nodiscard]] std::expected<std::string, LLMError> performHttpPost(
        std::string_view endpoint,
        const std::string& json_payload) const;
};

} // namespace agent::client