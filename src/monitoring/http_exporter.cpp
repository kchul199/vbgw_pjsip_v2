// prometheus-cpp Exposer를 실제로 여는 얇은 구현.
//
// 현재 VBGW의 주된 상태 노출은 HttpServer의 /metrics 이지만,
// exporter를 분리하고 싶을 때 어디서 registry를 바인딩하는지 보여주는 예제 역할도 한다.
#include "http_exporter.h"
#include "metrics_exporter.h"
#include <spdlog/spdlog.h>

namespace vbgw {
namespace monitoring {

HttpExporter::HttpExporter(const std::string& bind_address) {
    try {
        exposer_ = std::make_unique<prometheus::Exposer>(bind_address);
        exposer_->RegisterCollectable(MetricsExporter::instance().registry());
        spdlog::info("Prometheus HTTP Exporter started on {}", bind_address);
    } catch (const std::exception& e) {
        spdlog::error("Failed to start Prometheus HTTP Exporter: {}", e.what());
    }
}

} // namespace monitoring
} // namespace vbgw
