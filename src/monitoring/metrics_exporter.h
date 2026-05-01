// prometheus-cpp Registry와 주요 메트릭 family를 보관하는 인터페이스.
//
// RuntimeMetrics가 "애플리케이션 내부 상태"라면,
// 이 파일은 "외부 Prometheus 라이브러리 객체"를 감싼다고 보면 된다.
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
