// OpenTelemetry tracer 초기화와 조회를 감싸는 인터페이스.
//
// 현재 구현은 예제 수준이지만, 분산 추적을 붙일 때 진입점이 되는 파일이다.
#pragma once

#include <opentelemetry/trace/tracer.h>
#include <memory>
#include <string>

namespace vbgw {
namespace monitoring {

class OtelTracer {
public:
    static void Init(const std::string& service_name);
    static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetTracer();
};

} // namespace monitoring
} // namespace vbgw
