// prometheus-cpp Registry와 기본 metric family를 초기화하는 구현.
//
// 런타임 경로에서는 이미 생성된 Counter/Gauge 포인터만 재사용하므로,
// 생성 비용과 이름 충돌 관리는 이 파일의 constructor에서 한 번에 처리한다.
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
    // Registry와 family를 constructor에서 모두 구성해두면
    // 이후 호출자는 "메트릭 조회 후 값 변경"만 신경 쓰면 된다.
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
