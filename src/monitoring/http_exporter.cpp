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
