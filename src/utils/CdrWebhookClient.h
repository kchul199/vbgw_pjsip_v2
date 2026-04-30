#pragma once

#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>

struct WebhookPayload {
    std::string url;
    std::string json_data;
};

/**
 * @brief 비동기 CDR Webhook 클라이언트 (libcurl 기반)
 * 
 * 통화 종료 시 생성되는 CDR 데이터를 PJSIP 스레드를 블로킹하지 않고
 * 백그라운드 워커 스레드에서 외부 HTTP API로 전송합니다.
 */
class CdrWebhookClient {
public:
    static CdrWebhookClient& getInstance() {
        static CdrWebhookClient instance;
        return instance;
    }

    // 서버 시작 (워커 스레드 구동)
    void start();
    
    // 서버 종료
    void stop();

    // Webhook 큐에 CDR 추가 (Non-blocking)
    void pushCdr(const std::string& url, const std::string& json_data);

private:
    CdrWebhookClient();
    ~CdrWebhookClient();
    CdrWebhookClient(const CdrWebhookClient&) = delete;
    CdrWebhookClient& operator=(const CdrWebhookClient&) = delete;

    void workerLoop();

    std::queue<WebhookPayload> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> is_running_{false};
};
