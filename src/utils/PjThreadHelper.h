// PJSIP 외부 스레드 등록을 표준화하는 작은 유틸리티.
//
// HTTP worker, gRPC worker, timer callback처럼 PJSIP가 만든 스레드가 아닌 곳에서
// PJSIP API를 건드리기 전에 반드시 거쳐야 하는 안전장치다.
#pragma once

#include <pjsua2.hpp>
#include <pj/os.h>
#include <string>

namespace vbgw {
namespace utils {

// 외부 스레드를 PJSIP 런타임에 등록하는 정적 헬퍼.
//
// 호출해도 이미 등록된 스레드면 조용히 지나가도록 만들어,
// "필요한 곳마다 안전하게 한 번 더 호출"하는 패턴을 허용한다.
class PjThreadHelper {
public:
    // PJSIP 함수(미디어 제어, 통화 상태 변경 등)를 호출하는 외부 스레드는
    // 반드시 시작 지점에서 이 함수를 호출하여 스레드를 PJSIP 런타임에 등록해야 합니다.
    static void registerThread(const std::string& thread_name) {
        if (!pj_thread_is_registered()) {
            try {
                pj::Endpoint::instance().libRegisterThread(thread_name);
            } catch (const pj::Error& e) {
                // Ignore if already registered or endpoint not ready
            } catch (...) {}
        }
    }
};

} // namespace utils
} // namespace vbgw
