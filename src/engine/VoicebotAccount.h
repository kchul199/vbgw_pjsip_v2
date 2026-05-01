// PJSUA2 Account를 상속받아 "SIP 계정 + 인입/발신 콜 진입점" 역할을 정의한다.
//
// 인입호는 onIncomingCall()에서 라우팅/용량 검사를 거쳐 VoicebotCall로 연결되고,
// 발신호는 Admin API 등 외부 경로에서 makeOutboundCall()을 통해 시작된다.
#pragma once
#include <pjsua2.hpp>

#include <future>
#include <mutex>
#include <string>
#include <vector>

// SIP 계정 단위의 이벤트 핸들러.
//
// 운영 관점에서는 "어느 PBX 계정으로 등록되었고 어떤 통화를 만드는가"를 담당하며,
// 개발 관점에서는 라우팅 정책과 세션 생성 시점이 가장 많이 수정되는 파일 중 하나다.
class VoicebotAccount : public pj::Account
{
public:
    VoicebotAccount();
    ~VoicebotAccount() override;

    virtual void onRegState(pj::OnRegStateParam& prm) override;
    virtual void onIncomingCall(pj::OnIncomingCallParam& iprm) override;

    // HTTP Admin API 등 외부 제어 경로에서 발신 콜 시작
    bool makeOutboundCall(const std::string& target_uri, std::string* out_session_id = nullptr,
                          std::string* error_message = nullptr);

    // [Shutdown Fix] PJSIP Account를 명시적으로 정리
    // main() Graceful Shutdown 시 ep.shutdown() 전에 호출
    // 비동기 answer future를 대기하고, PJSIP Account 등록 해제
    void shutdown();

    // [Phase 2] 비동기 응답 처리 헬퍼
    void asyncAnswerCall(std::shared_ptr<pj::Call> call, const std::string& session_id, int answer_delay_ms);

private:
    // [C-3 Fix] detach() 대신 future 보관 — 소멸자에서 완료 보장
    std::mutex futures_mutex_;
    std::vector<std::future<void>> answer_futures_;

    // Outbound makeCall 직렬화 (PJSIP account thread-safety 보호)
    std::mutex outbound_mutex_;

    // shutdown()이 이미 호출되었는지 추적 — 소멸자에서의 이중 정리 방지
    bool shutdown_called_ = false;
};
