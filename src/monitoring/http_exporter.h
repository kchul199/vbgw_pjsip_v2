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
