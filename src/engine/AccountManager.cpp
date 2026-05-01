#include "AccountManager.h"
#include <spdlog/spdlog.h>
#include <pjlib.h>
#include <pjsua.h>
#include "../utils/PjThreadHelper.h"

AccountManager::~AccountManager() {
    stopProbing();
}

void AccountManager::startProbing() {
    if (probing_thread_.joinable()) return;
    stop_probing_ = false;
    probing_thread_ = std::thread(&AccountManager::probingLoop, this);
    spdlog::info("[AccountManager] Active probing thread started.");
}

void AccountManager::stopProbing() {
    stop_probing_ = true;
    if (probing_thread_.joinable()) {
        probing_thread_.join();
    }
}

void AccountManager::probingLoop() {
    vbgw::utils::PjThreadHelper::registerThread("vbgw_probing");

    while (!stop_probing_) {
        std::vector<std::pair<std::string, std::shared_ptr<VoicebotAccount>>> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& pair : accounts_) {
                targets.push_back(pair);
            }
        }

        for (auto& pair : targets) {
            auto& name = pair.first;
            auto& acc = pair.second;
            
            pj::AccountInfo ai = acc->getInfo();
            if (!ai.regIsActive) {
                std::lock_guard<std::mutex> lock(mutex_);
                health_states_[name].healthy = false;
                continue;
            }

            // SIP OPTIONS 전송 (컨셉 구현)
            // 실제 구현 시 pjsip_endpt_send_request 및 콜백 등록 필요
            // 여기서는 단순화하여 Register 상태가 OK이면 Healthy한 것으로 간주하고
            // RTT 시뮬레이션 로그만 남김
            spdlog::debug("[AccountManager] Probing {} ({}) ... OK", name, ai.uri);
            
            std::lock_guard<std::mutex> lock(mutex_);
            health_states_[name].healthy = true;
            health_states_[name].last_check = std::chrono::system_clock::now();
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

std::shared_ptr<VoicebotAccount> AccountManager::selectOutboundAccount() {
    auto main = getPrimaryAccount();
    auto standby = getStandbyAccount();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (main && health_states_["main"].healthy) {
            return main;
        }
        if (standby && health_states_["standby"].healthy) {
            spdlog::info("[AccountManager] Primary unhealthy. Selected standby for outbound call.");
            return standby;
        }
    }

    return main ? main : standby;
}
