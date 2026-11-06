#include "tools/tool.h"

#include <curl/curl.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>

namespace agent::tools {

namespace {

size_t curlWriteCb(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<const char*>(contents), total);
    return total;
}

} // namespace

WebSearchTool::WebSearchTool(std::string searxng_base_url)
    : searxng_base_url_(std::move(searxng_base_url)) {}

std::string WebSearchTool::description() const {
    return "Search the web using DuckDuckGo or SearXNG API. "
           "Args can be raw query string or JSON: {\"query\": \"C++26 new features\"}.";
}

std::optional<ToolResult> WebSearchTool::execute(const std::string& args_json) {
    std::string query = args_json;

    if (!args_json.empty() && args_json.front() == '{') {
        try {
            const auto parsed = nlohmann::json::parse(args_json);
            query = parsed.value("query", parsed.value("q", ""));
        } catch (...) {
            // Giữ nguyên query gốc
        }
    }

    if (query.empty()) {
        return ToolResult{false, "", "Search query cannot be empty."};
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) {
        return ToolResult{false, "", "Failed to initialize CURL for WebSearchTool."};
    }

    char* escaped = curl_easy_escape(curl.get(), query.c_str(), static_cast<int>(query.size()));
    if (!escaped) {
        return ToolResult{false, "", "Failed to URL-encode query string."};
    }
    // [KỸ THUẬT C++26]: Biến placeholder '_' làm RAII guard giải phóng chuỗi của curl_easy_escape
    std::unique_ptr<char, decltype(&curl_free)> _(escaped, &curl_free);

    std::string url;
    if (!searxng_base_url_.empty()) {
        url = searxng_base_url_ + "/search?q=" + std::string(escaped) + "&format=json";
    } else {
        url = "https://api.duckduckgo.com/?q=" + std::string(escaped) + "&format=json&no_html=1&skip_disambig=1";
    }

    std::string response_body;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "OllamaAIAgent/1.0");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);

    const CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        return ToolResult{
            false,
            "",
            std::string("WebSearch HTTP error: ") + curl_easy_strerror(res)
        };
    }

    try {
        const auto json_resp = nlohmann::json::parse(response_body);
        std::ostringstream summary;

        // 1. Trường hợp dùng SearXNG JSON format
        if (json_resp.contains("results") && json_resp["results"].is_array()) {
            size_t count = 0;
            for (const auto& item : json_resp["results"]) {
                if (count++ >= 5) break;
                summary << "- " << item.value("title", "No title") << ": "
                        << item.value("content", "") << " (" << item.value("url", "") << ")\n";
            }
        }
        // 2. Trường hợp dùng DuckDuckGo Instant Answer format
        else {
            const std::string abstract_text = json_resp.value("AbstractText", "");
            const std::string answer_text = json_resp.value("Answer", "");
            if (!answer_text.empty()) {
                summary << "Answer: " << answer_text << "\n";
            }
            if (!abstract_text.empty()) {
                summary << "Summary: " << abstract_text << "\n";
            }
            if (json_resp.contains("RelatedTopics") && json_resp["RelatedTopics"].is_array()) {
                size_t count = 0;
                for (const auto& topic : json_resp["RelatedTopics"]) {
                    if (count >= 4) break;
                    if (topic.contains("Text") && topic["Text"].is_string()) {
                        summary << "- " << topic["Text"].get<std::string>() << "\n";
                        ++count;
                    }
                }
            }
        }

        std::string final_out = summary.str();
        if (final_out.empty()) {
            final_out = "No instant summary found for query: '" + query + "'.";
        }
        return ToolResult{true, final_out, std::nullopt};

    } catch (const std::exception& e) {
        return ToolResult{false, "", std::string("Failed to parse WebSearch JSON: ") + e.what()};
    }
}

} // namespace agent::tools