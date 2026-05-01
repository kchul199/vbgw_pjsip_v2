#pragma once

#include <pjsua2.hpp>
#include <pj/os.h>
#include <string>

namespace vbgw {
namespace utils {

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
