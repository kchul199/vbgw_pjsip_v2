// PBX main/standby 계정 집합을 관리하는 매니저 인터페이스.
//
// 현재 구현은 "등록 상태 중심 건강도 판단 + outbound account 선택"에 초점을 둔다.
// 완전한 SIP OPTIONS 기반 active probe로 오해하지 않도록 읽는 것이 중요하다.
#pragma once

#include "VoicebotAccount.h"
#include <memory>
#include <mutex>
#include <unordered_map>

// 여러 VoicebotAccount를 묶어 health state와 선택 로직을 제공한다.
//
// 향후 active-active, 가중치 기반 라우팅, 진짜 SIP probe로 확장하더라도
// 외부에서는 이 매니저를 통해 account를 선택하도록 유지하는 것이 바람직하다.
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
