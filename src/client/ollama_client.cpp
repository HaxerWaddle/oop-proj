#include "client/ollama_client.h"

#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace agent::client {

namespace {

// Callback nhận dữ liệu trả về từ libcurl
size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total_size = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<const char*>(contents), total_size);
    return total_size;
}

// Bộ khởi tạo libcurl toàn cục an toàn theo cơ chế RAII
struct CurlGlobalInitializer {
    CurlGlobalInitializer() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalInitializer() { curl_global_cleanup(); }
};

// Tự động gọi một lần duy nhất khi chương trình khởi chạy
const CurlGlobalInitializer g_curl_init{};

} // namespace

// ==============================================================================
// Triển khai các hàm tiện ích Base64 trong LLMClient
// ==============================================================================

std::string LLMClient::encodeBase64(std::span<const unsigned char> data) {
    static constexpr char kBase64Chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string encoded;
    encoded.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size()) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                                (static_cast<uint32_t>(data[i + 1]) << 8) |
                                static_cast<uint32_t>(data[i + 2]);
        encoded.push_back(kBase64Chars[(triple >> 18) & 0x3F]);
        encoded.push_back(kBase64Chars[(triple >> 12) & 0x3F]);
        encoded.push_back(kBase64Chars[(triple >> 6) & 0x3F]);
        encoded.push_back(kBase64Chars[triple & 0x3F]);
        i += 3;
    }

    if (i < data.size()) {
        uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) {
            triple |= static_cast<uint32_t>(data[i + 1]) << 8;
        }

        encoded.push_back(kBase64Chars[(triple >> 18) & 0x3F]);
        encoded.push_back(kBase64Chars[(triple >> 12) & 0x3F]);
        if (i + 1 < data.size()) {
            encoded.push_back(kBase64Chars[(triple >> 6) & 0x3F]);
        } else {
            encoded.push_back('=');
        }
        encoded.push_back('=');
    }

    return encoded;
}

std::expected<std::string, LLMError> LLMClient::encodeImageFileToBase64(
    const std::filesystem::path& image_path) {
    // [KỸ THUẬT C++17]: std::filesystem kiểm tra sự tồn tại của file
    std::error_code ec;
    if (!std::filesystem::exists(image_path, ec) || !std::filesystem::is_regular_file(image_path, ec)) {
        return std::unexpected(LLMError{
            LLMErrorCode::FileReadError,
            "Image file does not exist: " + image_path.string()
        });
    }

    std::ifstream file(image_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::unexpected(LLMError{
            LLMErrorCode::FileReadError,
            "Failed to open image file: " + image_path.string()
        });
    }

    const auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        return std::unexpected(LLMError{
            LLMErrorCode::FileReadError,
            "Error reading bytes from image file: " + image_path.string()
        });
    }

    // [KỸ THUẬT C++20]: Truyền std::span vào hàm encodeBase64
    return encodeBase64(std::span<const unsigned char>{buffer});
}

// ==============================================================================
// Triển khai OllamaClient
// ==============================================================================

OllamaClient::OllamaClient(LLMConfig config) : config_(std::move(config)) {
    normalizeBaseUrl();
}

void OllamaClient::setConfig(const LLMConfig& config) {
    config_ = config;
    normalizeBaseUrl();
}

const LLMConfig& OllamaClient::getConfig() const noexcept {
    return config_;
}

void OllamaClient::normalizeBaseUrl() {
    // [KỸ THUẬT C++20]: std::string::ends_with kiểm tra hậu tố chuỗi
    while (config_.base_url.ends_with('/')) {
        config_.base_url.pop_back();
    }
}

std::expected<std::string, LLMError> OllamaClient::performHttpPost(
    std::string_view endpoint,
    const std::string& json_payload) const {
    
    // [KỸ THUẬT C++17]: Quản lý bộ nhớ CURL bằng std::unique_ptr với Custom Deleter
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) {
        return std::unexpected(LLMError{
            LLMErrorCode::Unknown,
            "Failed to initialize libcurl session."
        });
    }

    const std::string full_url = config_.base_url + std::string(endpoint);
    std::string response_buffer;

    struct curl_slist* raw_headers = nullptr;
    raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
    raw_headers = curl_slist_append(raw_headers, "Accept: application/json");
    raw_headers = curl_slist_append(raw_headers, "ngrok-skip-browser-warning: 1");
    raw_headers = curl_slist_append(raw_headers, "User-Agent: OllamaAgentClient/1.0");

    // [KỸ THUẬT C++26 - P2169R4]: Dùng biến placeholder '_' với std::unique_ptr
    // làm RAII Scope Guard để tự động giải phóng curl_slist khi ra khỏi hàm
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> _(raw_headers, &curl_slist_free_all);

    curl_easy_setopt(curl.get(), CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, raw_headers);
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_payload.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(json_payload.size()));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_buffer);

    // Bỏ qua xác thực SSL CA Certificate khi chạy kiểm thử qua ngrok hoặc HTTPS self-signed trên Windows
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);

    // Cấu hình timeout nghiêm ngặt
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, config_.timeout_seconds);

    const CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        if (res == CURLE_OPERATION_TIMEDOUT) {
            return std::unexpected(LLMError{
                LLMErrorCode::Timeout,
                std::string("Request timed out after ") + std::to_string(config_.timeout_seconds) + "s: " + curl_easy_strerror(res)
            });
        }
        if (res == CURLE_COULDNT_CONNECT) {
            return std::unexpected(LLMError{
                LLMErrorCode::ConnectionRefused,
                std::string("Connection refused by server (") + full_url + "): " + curl_easy_strerror(res)
            });
        }
        return std::unexpected(LLMError{
            LLMErrorCode::Unknown,
            std::string("CURL error: ") + curl_easy_strerror(res)
        });
    }

    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        return std::unexpected(LLMError{
            LLMErrorCode::HttpError,
            "HTTP Status " + std::to_string(http_code) + " from Ollama: " + response_buffer
        });
    }

    return response_buffer;
}

std::expected<LLMResponse, LLMError> OllamaClient::chat(
    const std::vector<Message>& messages) {
    
    using json = nlohmann::json;

    // 1. Xây dựng JSON payload theo đúng đặc tả Ollama /api/chat
    json payload;
    payload["model"] = config_.model_name;
    payload["stream"] = false;
    payload["options"] = {
        {"temperature", config_.temperature},
        {"num_predict", config_.max_tokens}
    };

    json msg_array = json::array();
    // [KỸ THUẬT C++17]: Range-based for + Structured Bindings unpack các trường của Message
    for (const auto& [role, content, images_base64] : messages) {
        json msg_obj = {
            {"role", role},
            {"content", content}
        };
        // Hỗ trợ Multimodal: nếu tin nhắn có đính kèm ảnh Base64 thì thêm trường "images"
        if (!images_base64.empty()) {
            msg_obj["images"] = images_base64;
        }
        msg_array.push_back(std::move(msg_obj));
    }
    payload["messages"] = std::move(msg_array);

    // 2. Đo latency và gửi HTTP POST
    const auto start_time = std::chrono::steady_clock::now();
    auto raw_response_exp = performHttpPost("/api/chat", payload.dump());
    const auto end_time = std::chrono::steady_clock::now();

    const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    if (!raw_response_exp.has_value()) {
        return std::unexpected(raw_response_exp.error());
    }

    // 3. Parse JSON response và xử lý ngoại lệ Malformed JSON
    try {
        const json resp_json = json::parse(raw_response_exp.value());

        if (!resp_json.contains("message") || !resp_json["message"].contains("content")) {
            return std::unexpected(LLMError{
                LLMErrorCode::MalformedJson,
                "Missing 'message.content' field in Ollama JSON response."
            });
        }

        LLMResponse result;
        result.content = resp_json["message"]["content"].get<std::string>();
        result.prompt_tokens = resp_json.value("prompt_eval_count", 0);
        result.completion_tokens = resp_json.value("eval_count", 0);
        result.total_tokens = result.prompt_tokens + result.completion_tokens;
        result.latency_ms = static_cast<long>(latency_ms);

        return result;

    } catch (const json::parse_error& e) {
        return std::unexpected(LLMError{
            LLMErrorCode::MalformedJson,
            std::string("Malformed JSON response from Ollama: ") + e.what()
        });
    } catch (const json::type_error& e) {
        return std::unexpected(LLMError{
            LLMErrorCode::MalformedJson,
            std::string("JSON type mismatch in Ollama response: ") + e.what()
        });
    }
}

} // namespace agent::client