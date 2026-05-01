// OpenTelemetry tracer provider를 전역 등록하는 최소 구현.
//
// 현재는 stdout exporter 예제지만, OTLP 등 실제 백엔드로 바꿀 때도
// 초기화 진입점은 이 파일 한 곳으로 유지하는 것이 좋다.
#include "otel_tracer.h"
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/exporters/ostream/span_exporter_factory.h>

namespace vbgw {
namespace monitoring {

void OtelTracer::Init(const std::string& service_name) {
    // 예제 단계에서는 stdout exporter를 사용하지만,
    // 운영 연동 시 exporter/processor 교체 지점도 바로 여기다.
    auto exporter = opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create();
    auto processor = opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
    auto provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(std::move(processor));
    std::shared_ptr<opentelemetry::trace::TracerProvider> api_provider = std::move(provider);
    opentelemetry::trace::Provider::SetTracerProvider(api_provider);
}

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> OtelTracer::GetTracer() {
    auto provider = opentelemetry::trace::Provider::GetTracerProvider();
    return provider->GetTracer("vbgw_tracer");
}

} // namespace monitoring
} // namespace vbgw
