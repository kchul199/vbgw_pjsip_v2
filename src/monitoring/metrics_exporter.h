#pragma once

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>
#include <memory>

namespace vbgw {
namespace monitoring {

class MetricsExporter {
public:
    static MetricsExporter& instance();

    // Registry accessor
    std::shared_ptr<prometheus::Registry> registry() const { return registry_; }

    // Metric getters
    prometheus::Counter& sip_sessions();
    prometheus::Counter& rtp_packet_loss();
    prometheus::Gauge& grpc_latency_ms();
    prometheus::Counter& vad_processed();

private:
    MetricsExporter();
    ~MetricsExporter() = default;
    MetricsExporter(const MetricsExporter&) = delete;
    MetricsExporter& operator=(const MetricsExporter&) = delete;

    std::shared_ptr<prometheus::Registry> registry_;
    prometheus::Counter* sip_sessions_counter_ = nullptr;
    prometheus::Counter* rtp_packet_loss_counter_ = nullptr;
    prometheus::Gauge* grpc_latency_gauge_ = nullptr;
    prometheus::Counter* vad_processed_counter_ = nullptr;
};

} // namespace monitoring
} // namespace vbgw
