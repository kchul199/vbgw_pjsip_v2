#include "SessionManager.h"
#include "../utils/AppConfig.h"

#include <spdlog/spdlog.h>
#include <sw/redis++/redis++.h>

SessionManager& SessionManager::getInstance() {
    static SessionManager instance;
    return instance;
}

SessionManager::SessionManager() : max_calls_(AppConfig::instance().max_concurrent_calls) {
    try {
        const auto& cfg = AppConfig::instance();
        if (!cfg.redis_addr.empty()) {
            redis_ = std::make_unique<sw::redis::Redis>(cfg.redis_addr);
            spdlog::info("[SessionManager] Connected to Redis at {}", cfg.redis_addr);
        } else {
            spdlog::warn("[SessionManager] Redis address not configured, distributed session tracking disabled.");
        }
    } catch (const std::exception& e) {
        spdlog::error("[SessionManager] Failed to connect to Redis: {}", e.what());
    }
}

std::vector<std::shared_ptr<VoicebotCall>> SessionManager::cleanupZombiesLocked() {
    std::vector<std::shared_ptr<VoicebotCall>> to_delete;
    auto now = std::chrono::steady_clock::now();
    for (auto it = zombies_.begin(); it != zombies_.end(); ) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - it->first).count() >= 30) {
            to_delete.push_back(std::move(it->second));
            it = zombies_.erase(it);
        } else {
            ++it;
        }
    }
    return to_delete;
}

SessionManager::~SessionManager() = default;

void SessionManager::addCall(const std::string& session_id, std::shared_ptr<VoicebotCall> call) {
    std::vector<std::shared_ptr<VoicebotCall>> to_delete;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        calls_[session_id] = call;
        to_delete = cleanupZombiesLocked();
    }
    
    if (redis_) {
        try {
            redis_->set("call:" + session_id, "active");
        } catch (...) {}
    }
}

bool SessionManager::tryAddCall(const std::string& session_id, std::shared_ptr<VoicebotCall> call) {
    std::vector<std::shared_ptr<VoicebotCall>> to_delete;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (calls_.size() >= static_cast<size_t>(max_calls_))
            return false;
        calls_[session_id] = call;
        to_delete = cleanupZombiesLocked();
    }
    
    if (redis_) {
        try {
            redis_->set("call:" + session_id, "active");
        } catch (...) {}
    }
    return true;
}

void SessionManager::removeCall(const std::string& session_id) {
    std::vector<std::shared_ptr<VoicebotCall>> to_delete;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calls_.find(session_id);
        if (it != calls_.end()) {
            zombies_.push_back({std::chrono::steady_clock::now(), std::move(it->second)});
            calls_.erase(it);
        }
        to_delete = cleanupZombiesLocked();
    }
    
    if (redis_) {
        try {
            redis_->del("call:" + session_id);
        } catch (...) {}
    }
}

std::shared_ptr<VoicebotCall> SessionManager::takeCallByPjsipId(int pjsip_call_id,
                                                                std::string* session_id) {
    std::shared_ptr<VoicebotCall> extracted;
    std::vector<std::shared_ptr<VoicebotCall>> to_delete;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = calls_.begin(); it != calls_.end(); ++it) {
            if (!it->second || it->second->getInitialCallId() != pjsip_call_id) {
                continue;
            }

            if (session_id) {
                *session_id = it->first;
            }
            extracted = std::move(it->second);
            zombies_.push_back({std::chrono::steady_clock::now(), extracted});
            calls_.erase(it);
            break;
        }
        to_delete = cleanupZombiesLocked();
    }

    if (extracted && redis_) {
        try {
            redis_->del("call:" + extracted->getSessionId());
        } catch (...) {}
    }

    return extracted;
}

std::shared_ptr<VoicebotCall> SessionManager::getCall(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = calls_.find(session_id);
    if (it != calls_.end()) {
        return it->second;
    }
    return nullptr;
}

bool SessionManager::canAcceptCall() {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_.size() < static_cast<size_t>(max_calls_);
}

size_t SessionManager::getActiveCallCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_.size();
}

std::vector<std::shared_ptr<VoicebotCall>> SessionManager::getActiveCallsSnapshot() {
    std::vector<std::shared_ptr<VoicebotCall>> active_calls;
    std::lock_guard<std::mutex> lock(mutex_);
    active_calls.reserve(calls_.size());
    for (auto& pair : calls_) {
        active_calls.push_back(pair.second);
    }
    return active_calls;
}

void SessionManager::hangupAllCalls() {
    std::vector<std::shared_ptr<VoicebotCall>> active_calls;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : calls_) {
            active_calls.push_back(pair.second);
        }
    }
    for (auto& call : active_calls) {
        try {
            pj::CallOpParam prm;
            prm.statusCode = PJSIP_SC_DECLINE;
            call->hangup(prm);
        } catch (...) {
        }
    }
}

void SessionManager::endAllAiSessions() {
    std::vector<std::shared_ptr<VoicebotCall>> active_calls;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : calls_) {
            active_calls.push_back(pair.second);
        }
    }
    for (auto& call : active_calls) {
        try {
            call->endAiSession();
        } catch (...) {
        }
    }
}

void SessionManager::clearAllCalls() {
    std::lock_guard<std::mutex> lock(mutex_);
    calls_.clear();
    zombies_.clear();
}
