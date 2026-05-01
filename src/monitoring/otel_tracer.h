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
