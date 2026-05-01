#pragma once
#include "../utils/AppConfig.h"
#include "VoicebotCall.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace sw { namespace redis { class Redis; } }

// 싱글톤 기반의 쓰레드 세이프 콜 라이프사이클 관리자
class SessionManager
{
public:
    static SessionManager& getInstance();

    void addCall(const std::string& session_id, std::shared_ptr<VoicebotCall> call);
    bool tryAddCall(const std::string& session_id, std::shared_ptr<VoicebotCall> call);
    void removeCall(const std::string& session_id);
    std::shared_ptr<VoicebotCall> takeCallByPjsipId(int pjsip_call_id,
                                                    std::string* session_id = nullptr);
    std::shared_ptr<VoicebotCall> getCall(const std::string& session_id);
    bool canAcceptCall();
    size_t getActiveCallCount();
    std::vector<std::shared_ptr<VoicebotCall>> getActiveCallsSnapshot();
    void hangupAllCalls();
    void endAllAiSessions();
    void clearAllCalls();

private:
    std::vector<std::shared_ptr<VoicebotCall>> cleanupZombiesLocked();
    SessionManager();
    ~SessionManager();
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    const int max_calls_;
    std::unordered_map<std::string, std::shared_ptr<VoicebotCall>> calls_;
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::shared_ptr<VoicebotCall>>> zombies_;
    std::mutex mutex_;
    std::unique_ptr<sw::redis::Redis> redis_;
};
