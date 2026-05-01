// prometheus-cpp Exposer 래퍼 인터페이스.
//
// 현재 VBGW는 자체 /metrics 엔드포인트도 제공하지만,
// 별도 Prometheus exporter 구성을 시도할 때 사용할 수 있는 추상화다.
#pragma once

#include <prometheus/exposer.h>
#include <memory>
#include <string>

namespace vbgw {
namespace monitoring {

class HttpExporter {
public:
    HttpExporter(const std::string& bind_address);
    ~HttpExporter() = default;

private:
    std::unique_ptr<prometheus::Exposer> exposer_;
};

} // namespace monitoring
} // namespace vbgw
