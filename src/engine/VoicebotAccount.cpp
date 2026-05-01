#include "VoicebotAccount.h"

#include "../utils/AppConfig.h"
#include "../utils/RuntimeMetrics.h"
#include "../utils/PjThreadHelper.h"
#include "SessionManager.h"
#include "VoicebotCall.h"
#include "RoutingEngine.h"
#include "CapacityManager.h"

#include <pjlib.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <future>

using namespace pj;

namespace {
// 중복 로직을 헬퍼 함수로 분리
void handleAcceptedIncomingCall(VoicebotAccount* acc, OnIncomingCallParam& iprm, std::shared_ptr<VoicebotCall> call)
{
    // 180 Ringing 전송 — PBX에 수신 알림
    try {
        CallOpParam ringing_prm;
        ringing_prm.statusCode = PJSIP_SC_RINGING;
        call->answer(ringing_prm);
        spdlog::info("[Account] Sent 180 Ringing for Call-ID: {}", iprm.callId);
    } catch (Error& err) {
        spdlog::error("[Account] Failed to send 180 Ringing: {}", err.info());
    }

    // [H-6 Fix] ANSWER_DELAY_MS를 AppConfig에서 캐싱된 값으로 읽기
    const int answer_delay_ms = AppConfig::instance().answer_delay_ms;
    {
        acc->asyncAnswerCall(call, call->getSessionId(), answer_delay_ms);
    }
}
}

VoicebotAccount::VoicebotAccount() {}

VoicebotAccount::~VoicebotAccount()
{
    // [Shutdown Fix] shutdown()이 이미 호출되었으면 스킵 — 이중 정리 방지
    if (!shutdown_called_) {
        shutdown();
    }
}

void VoicebotAccount::shutdown()
{
    if (shutdown_called_) {
        return;
    }
    shutdown_called_ = true;

    // [L-1 Fix] futures를 swap으로 꺼낸 뒤 mutex 해제 후 wait — deadlock 방지
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lock(futures_mutex_);
        pending.swap(answer_futures_);
    }
    for (auto& f : pending) {
        if (f.valid()) {
            f.wait();
        }
    }

    spdlog::info("[Account] Shutdown complete — {} pending futures waited.", pending.size());
}

void VoicebotAccount::onRegState(OnRegStateParam& prm)
{
    AccountInfo ai = getInfo();
    RuntimeMetrics::instance().setSipRegistration(ai.regIsActive, static_cast<int>(prm.code));

    if (ai.regIsActive) {
        spdlog::info("[Account] Registered: {} (status={})", ai.uri, static_cast<int>(prm.code));
    } else {
        // [M-3 Fix] SIP 등록 해제 시 재등록 안내 로그
        // PJSIP는 AccountConfig의 regConfig.retryIntervalSec을 통해 자동 재등록을 지원하며,
        // 기본값이 0(재시도 안 함)이므로 create() 시 설정하는 것이 권장됨.
        // 현재는 경고를 상세하게 남겨 운영자가 인지할 수 있도록 함.
        spdlog::warn("[Account] Unregistered: {} (status={}) — PBX may be unreachable. "
                     "PJSIP will retry based on regConfig.retryIntervalSec setting.",
                     ai.uri, static_cast<int>(prm.code));
    }
}

void VoicebotAccount::asyncAnswerCall(std::shared_ptr<pj::Call> call, const std::string& session_id, int answer_delay_ms)
{
    std::lock_guard<std::mutex> lock(futures_mutex_);

    // 완료된 future 정리 (메모리 누적 방지)
    answer_futures_.erase(std::remove_if(answer_futures_.begin(), answer_futures_.end(),
                                         [](const std::future<void>& f) {
                                             return f.wait_for(std::chrono::seconds(0)) ==
                                                    std::future_status::ready;
                                         }),
                          answer_futures_.end());

    // 200 OK 응답을 별도 스레드에서 비동기 처리
    answer_futures_.push_back(
        std::async(std::launch::async, [this, call, session_id, answer_delay_ms]() {
            vbgw::utils::PjThreadHelper::registerThread("vbgw_answer");

            std::this_thread::sleep_for(std::chrono::milliseconds(answer_delay_ms));
            try {
                pj::CallOpParam ok_prm;
                ok_prm.statusCode = PJSIP_SC_OK;
                call->answer(ok_prm);
                spdlog::info("[Account] Sent 200 OK for Session: {}", session_id);
            } catch (pj::Error& e) {
                spdlog::warn("[Account] Delay-Answer failed. Releasing Session={}. reason: {}",
                             session_id, e.info());
                SessionManager::getInstance().removeCall(session_id);
            } catch (...) {
                spdlog::error("[Account] Unknown error answering call {}", session_id);
                SessionManager::getInstance().removeCall(session_id);
            }
        }));
}

void VoicebotAccount::onIncomingCall(OnIncomingCallParam& iprm)
{
    // [Phase 0] destination_number 및 source_gateway 추출
    std::string destination_number;
    std::string source_gateway = "unknown"; // Default

    pjsip_rx_data *rdata = static_cast<pjsip_rx_data*>(iprm.rdata.pjRxData);
    if (rdata && rdata->msg_info.msg->type == PJSIP_REQUEST_MSG) {
        pjsip_uri *uri = rdata->msg_info.msg->line.req.uri;
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(uri);
        if (sip_uri) {
            destination_number.assign(sip_uri->user.ptr, sip_uri->user.slen);
        }

        // source_gateway 식별 (단순화: remote_name 사용)
        if (rdata->pkt_info.src_name[0] != '\0') {
            source_gateway.assign(rdata->pkt_info.src_name);
        }
    }

    spdlog::info("[Account] Incoming SIP call, Call-ID: {}, Dest: {}, From: {}", 
                 iprm.callId, destination_number, source_gateway);

    std::string stale_session_id;
    if (auto stale_call =
            SessionManager::getInstance().takeCallByPjsipId(iprm.callId, &stale_session_id)) {
        spdlog::warn("[Account] Reaping stale Session={} before reusing PJSIP Call-ID={}",
                     stale_session_id, iprm.callId);
        stale_call->reapWithoutDisconnect(
            "PJSIP call slot reused before DISCONNECTED callback");
    }

    // [Phase 0] RoutingEngine을 통한 라우팅 해석
    auto route = RoutingEngine::getInstance().resolveRoute(destination_number, "default-policy", source_gateway);

    if (route.matched) {
        // [Phase 2] CapacityManager를 통한 Slot 할당 시도
        auto lease = CapacityManager::getInstance().leaseSlot(route.service_name, route.capacity.max_concurrent);
        if (!lease.success) {
            spdlog::warn("[Account] Overflow for service {}. Policy: {}", route.service_name, route.capacity.overflow.policy);
            
            if (route.capacity.overflow.policy == "direct_transfer" && !route.capacity.overflow.target.empty()) {
                // [T-1] Direct Transfer (302 Redirect)
                spdlog::info("[Account] Redirecting overflow call to {}", route.capacity.overflow.target);
                
                pj::CallOpParam prm;
                prm.statusCode = PJSIP_SC_MOVED_TEMPORARILY;
                
                // [Step 4] Contact 헤더 동적 주입
                pj::SipHeader contact;
                contact.hName = "Contact";
                contact.hValue = "<" + route.capacity.overflow.target + ">";
                prm.txOption.headers.push_back(contact);

                // 180 Ringing 먼저 전송하여 호 진행 상태 알림 (일부 스택 호환성)
                pjsua_call_answer2(iprm.callId, NULL, PJSIP_SC_RINGING, NULL, NULL);

                // C API로 직접 응답 (PJSUA2 answer()는 3xx 처리에 제약이 있을 수 있음)

                pjsua_msg_data msg_data;
                pjsua_msg_data_init(&msg_data);
                
                // SipTxOption에서 pjsua_msg_data로 변환
                pj_pool_t* pool = pj_pool_create(pjsua_get_pool_factory(), "redirect", 512, 512, NULL);
                msg_data.hdr_list.next = msg_data.hdr_list.prev = &msg_data.hdr_list;
                
                pj_str_t h_name = pj_str((char*)contact.hName.c_str());
                pj_str_t h_value = pj_str((char*)contact.hValue.c_str());
                pjsip_generic_string_hdr* h = pjsip_generic_string_hdr_create(
                    pool, &h_name, &h_value);
                pj_list_insert_before(&msg_data.hdr_list, h);

                // 302 응답과 함께 연결 거절 (Redirection)
                pjsua_call_hangup(iprm.callId, prm.statusCode, NULL, &msg_data);
                
                pj_pool_release(pool);
                return;
            } else {
                pjsua_call_hangup(iprm.callId, PJSIP_SC_BUSY_HERE, NULL, NULL);
                return;
            }
        }
        
        // Slot 할당 성공
        auto call = std::make_shared<VoicebotCall>(*this, iprm.callId);
        call->setRoutingInfo(route.service_name, route.entry_number, route.route_type);
        call->setSlotId(lease.slot_id);

        if (!SessionManager::getInstance().tryAddCall(call->getSessionId(), call)) {
            CapacityManager::getInstance().releaseSlot(route.service_name, lease.slot_id);
            spdlog::warn("[Account] Max call limit reached. Rejecting call {} with 486 Busy Here.",
                         call->getSessionId());
            CallOpParam prm;
            prm.statusCode = PJSIP_SC_BUSY_HERE;
            try { call->hangup(prm); } catch(...) {}
            return;
        }

        handleAcceptedIncomingCall(this, iprm, call);
    } else {
        // Unknown entry: static fallback
        spdlog::warn("[Account] No route matched for Dest: {}. Fallback to static.", destination_number);
        
        auto call = std::make_shared<VoicebotCall>(*this, iprm.callId);
        if (!SessionManager::getInstance().tryAddCall(call->getSessionId(), call)) {
            pj::CallOpParam prm;
            prm.statusCode = PJSIP_SC_BUSY_HERE;
            try { call->hangup(prm); } catch(...) {}
            return;
        }
        handleAcceptedIncomingCall(this, iprm, call);
    }
}

bool VoicebotAccount::makeOutboundCall(const std::string& target_uri, std::string* out_session_id,
                                       std::string* error_message)
{
    std::lock_guard<std::mutex> lock(outbound_mutex_);

    if (!SessionManager::getInstance().canAcceptCall()) {
        if (error_message) {
            *error_message = "Maximum concurrent call limit reached";
        }
        spdlog::warn("[Account] Outbound call rejected (capacity): {}", target_uri);
        return false;
    }

    // 외부 스레드(HTTP worker)에서 PJSIP API 호출 시 스레드 등록 필요
    vbgw::utils::PjThreadHelper::registerThread("vbgw_outbound");

    auto call = std::make_shared<VoicebotCall>(*this);
    int call_id = PJSUA_INVALID_ID;

    try {
        CallOpParam prm(true);
        prm.opt.audioCount = 1;
        prm.opt.videoCount = 0;
        call->makeCall(target_uri, prm);

        // makeCall 성공 시 PJSIP call id가 할당되어 있어야 함
        call_id = call->getInfo().id;
        if (call_id == PJSUA_INVALID_ID) {
            if (error_message) {
                *error_message = "PJSIP returned invalid call id for outbound call";
            }
            spdlog::error("[Account] Outbound call created with invalid call id: {}", target_uri);
            return false;
        }

        if (!SessionManager::getInstance().tryAddCall(call->getSessionId(), call)) {
            if (error_message) {
                *error_message = "Maximum concurrent call limit reached after call allocation";
            }
            spdlog::warn("[Account] Outbound call race-capacity reject (call_id={}): {}", call_id,
                         target_uri);
            try {
                CallOpParam hangup_prm;
                hangup_prm.statusCode = PJSIP_SC_BUSY_HERE;
                call->hangup(hangup_prm);
            } catch (const pj::Error& e) {
                spdlog::debug("[Account] Outbound hangup suppressed pj::Error: {}", e.info());
            } catch (...) {
                spdlog::debug("[Account] Outbound hangup suppressed unknown error");
            }
            return false;
        }

        if (out_session_id) {
            *out_session_id = call->getSessionId();
        }

        spdlog::info("[Account] Outbound SIP call initiated [call_id={}, target_uri={}]", call_id,
                     target_uri);
        return true;
    } catch (Error& err) {
        if (error_message) {
            *error_message = err.info();
        }
        spdlog::error("[Account] Outbound call failed [target_uri={}]: {}", target_uri, err.info());
        if (call) {
            SessionManager::getInstance().removeCall(call->getSessionId());
        }
        return false;
    } catch (const std::exception& e) {
        if (error_message) {
            *error_message = e.what();
        }
        spdlog::error("[Account] Outbound call failed with std::exception [target_uri={}]: {}",
                      target_uri, e.what());
        if (call) {
            SessionManager::getInstance().removeCall(call->getSessionId());
        }
        return false;
    } catch (...) {
        if (error_message) {
            *error_message = "Unknown outbound call failure";
        }
        spdlog::error("[Account] Outbound call failed with unknown error [target_uri={}]",
                      target_uri);
        if (call) {
            SessionManager::getInstance().removeCall(call->getSessionId());
        }
        return false;
    }
}
