#include "CdrWebhookClient.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>

CdrWebhookClient::CdrWebhookClient() {
    curl_global_init(CURL_GLOBAL_ALL);
}

CdrWebhookClient::~CdrWebhookClient() {
    stop();
    curl_global_cleanup();
}

void CdrWebhookClient::start() {
    if (is_running_.exchange(true)) return;
    
    stop_ = false;
    worker_thread_ = std::thread(&CdrWebhookClient::workerLoop, this);
    spdlog::info("[CdrWebhookClient] Worker thread started.");
}

void CdrWebhookClient::stop() {
    if (!is_running_.exchange(false)) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    spdlog::info("[CdrWebhookClient] Worker thread stopped.");
}

void CdrWebhookClient::pushCdr(const std::string& url, const std::string& json_data) {
    if (url.empty() || json_data.empty()) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        queue_.push({url, json_data});
    }
    cv_.notify_one();
}

void CdrWebhookClient::workerLoop() {
    while (true) {
        WebhookPayload payload;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });

            if (stop_ && queue_.empty()) break;

            payload = std::move(queue_.front());
            queue_.pop();
        }

        // CURL을 이용한 POST 전송
        CURL* curl = curl_easy_init();
        if (curl) {
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, payload.url.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.json_data.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5초 타임아웃

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                spdlog::error("[CdrWebhookClient] Failed to send Webhook to {}: {}", 
                              payload.url, curl_easy_strerror(res));
                // 필요 시 재시도 큐에 다시 넣는 로직 추가 가능
            } else {
                long response_code;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                spdlog::debug("[CdrWebhookClient] Webhook sent successfully to {}. Code: {}", 
                              payload.url, response_code);
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
    }
}
