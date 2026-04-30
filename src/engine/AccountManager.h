#pragma once

#include "VoicebotAccount.h"
#include <memory>
#include <mutex>
#include <unordered_map>

class AccountManager {
public:
    static AccountManager& getInstance() {
        static AccountManager instance;
        return instance;
    }

    void addAccount(const std::string& name, std::shared_ptr<VoicebotAccount> account) {
        std::lock_guard<std::mutex> lock(mutex_);
        accounts_[name] = account;
    }

    std::shared_ptr<VoicebotAccount> getAccount(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = accounts_.find(name);
        if (it != accounts_.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::shared_ptr<VoicebotAccount> getPrimaryAccount() {
        return getAccount("main");
    }

    std::shared_ptr<VoicebotAccount> getStandbyAccount() {
        return getAccount("standby");
    }

    // Health-aware selection for outbound calls
    std::shared_ptr<VoicebotAccount> selectOutboundAccount();

    // [Step 4] Active Probing
    void startProbing();
    void stopProbing();

private:
    AccountManager() = default;
    ~AccountManager();
    AccountManager(const AccountManager&) = delete;
    AccountManager& operator=(const AccountManager&) = delete;

    void probingLoop();

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<VoicebotAccount>> accounts_;

    // Health states (RTT 등 관리 가능)
    struct HealthState {
        bool healthy = true;
        std::chrono::system_clock::time_point last_check;
    };
    std::unordered_map<std::string, HealthState> health_states_;

    std::thread probing_thread_;
    std::atomic<bool> stop_probing_{false};
};
