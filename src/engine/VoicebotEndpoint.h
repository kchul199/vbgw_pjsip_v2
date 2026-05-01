// PJSIP Endpoint 수명주기를 감싸는 인터페이스.
//
// main.cpp는 이 클래스를 통해 PJSIP를 초기화하고 transport를 열며,
// 계정/콜 객체는 그 위에서 동작한다. 따라서 이 파일은 "SIP 런타임 부트로더"에 가깝다.
#pragma once
#include <pjsua2.hpp>

#include <memory>
#include <string>
#include <vector>

// 애플리케이션 전체에서 하나만 존재하는 SIP endpoint 래퍼.
//
// 책임:
// 1. libCreate/libInit/libStart/libDestroy 순서 보장
// 2. UDP/TCP/TLS transport 생성
// 3. compile-time call slot cap과 runtime 설정 간 차이를 로그로 드러냄
class VoicebotEndpoint
{
public:
    VoicebotEndpoint();
    ~VoicebotEndpoint();

    bool init();
    bool start(int sip_port);
    void shutdown();
    pj::TransportId preferredTransportId() const;

    // [E-4] 코덱 우선순위 변경
    void setCodecPriority(const std::string& codec_id, short priority);

private:
    bool startTransport(pjsip_transport_type_e type, const pj::TransportConfig& cfg,
                        const std::string& label, pj::TransportId* out_id);
    void choosePreferredTransport();

    std::unique_ptr<pj::Endpoint> ep_;
    pj::TransportId udp_transport_id_ = PJSUA_INVALID_ID;
    pj::TransportId tcp_transport_id_ = PJSUA_INVALID_ID;
    pj::TransportId tls_transport_id_ = PJSUA_INVALID_ID;
    pj::TransportId preferred_transport_id_ = PJSUA_INVALID_ID;
    bool destroyed_ = false;  // [A-2 Fix] libDestroy() 이중 호출 방지
};
