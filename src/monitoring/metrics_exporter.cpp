#include "metrics_exporter.h"

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

namespace vbgw {
namespace monitoring {

MetricsExporter& MetricsExporter::instance() {
    static MetricsExporter singleton;
    return singleton;
}

MetricsExporter::MetricsExporter() {
    registry_ = std::make_shared<prometheus::Registry>();
    // SIP sessions counter
    auto& sip_counter_family = prometheus::BuildCounter()
        .Name("vbgw_sip_sessions_total")
        .Help("Total number of SIP sessions created")
        .Register(*registry_);
    sip_sessions_counter_ = &sip_counter_family.Add({});

    // RTP packet loss counter
    auto& rtp_counter_family = prometheus::BuildCounter()
        .Name("vbgw_rtp_packet_loss_total")
        .Help("Total number of lost RTP packets")
        .Register(*registry_);
    rtp_packet_loss_counter_ = &rtp_counter_family.Add({});

    // VAD processed counter
    auto& vad_counter_family = prometheus::BuildCounter()
        .Name("vbgw_vad_processed_total")
        .Help("Total number of VAD processed frames")
        .Register(*registry_);
    vad_processed_counter_ = &vad_counter_family.Add({});

    // gRPC latency gauge (ms)
    auto& grpc_gauge_family = prometheus::BuildGauge()
        .Name("vbgw_grpc_latency_ms")
        .Help("Current gRPC streaming latency in milliseconds")
        .Register(*registry_);
    grpc_latency_gauge_ = &grpc_gauge_family.Add({});
}

prometheus::Counter& MetricsExporter::sip_sessions() {
    return *sip_sessions_counter_;
}

prometheus::Counter& MetricsExporter::rtp_packet_loss() {
    return *rtp_packet_loss_counter_;
}

prometheus::Gauge& MetricsExporter::grpc_latency_ms() {
    return *grpc_latency_gauge_;
}

prometheus::Counter& MetricsExporter::vad_processed() {
    return *vad_processed_counter_;
}

} // namespace monitoring
} // namespace vbgw
