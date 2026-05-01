// main/standby SIP 계정의 health state와 선택 정책 구현.
//
// 현재 probing은 "등록 상태 기반의 단순 health 판단"에 가깝다.
// 따라서 이 파일을 볼 때는 완전한 active probing 시스템이 아니라,
// outbound account 선택을 위한 얇은 health layer로 이해하는 것이 맞다.
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
        // accounts_ 락을 오래 잡지 않기 위해 snapshot을 만든 뒤,
        // 실제 getInfo() 호출과 health 판정은 락 밖에서 진행한다.
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
    // 선택 정책은 단순하다.
    // healthy한 main 우선, 그렇지 않으면 healthy한 standby, 마지막 fallback은 main/standby 순.
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
