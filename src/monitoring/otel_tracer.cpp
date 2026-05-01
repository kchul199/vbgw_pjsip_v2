#include "otel_tracer.h"
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/exporters/ostream/span_exporter_factory.h>

namespace vbgw {
namespace monitoring {

void OtelTracer::Init(const std::string& service_name) {
    // For demonstration, use an ostream exporter (stdout)
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
