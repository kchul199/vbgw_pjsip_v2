#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

struct RoutingMatchRule {
    std::vector<std::string> ingress_stages;
    std::vector<std::string> source_gateways;
};

struct ServiceOverflow {
    std::string policy = "busy";
    int max_wait_seconds = 0;
    std::string announcement;
    std::string on_timeout = "busy";
    std::string target;
    bool announce_before_transfer = false;
};

struct ServiceCapacity {
    std::string backend = "logical"; // "logical" or "sip_extension"
    int max_concurrent = 0;
    std::string allocator = "round_robin";
    std::string overflow_policy = "busy";
    std::string transfer_target;
    ServiceOverflow overflow;
    std::vector<std::string> extensions;
    bool require_registered = false;
};

struct ServiceRoute {
    std::string name;
    bool enabled;
    std::string route_type;
    int priority = 0;
    std::vector<std::string> entry_numbers;
    RoutingMatchRule match;
    ServiceCapacity capacity;
};

struct ResolvedRoute {
    bool matched = false;
    std::string service_name;
    std::string route_type;
    std::string entry_number;
    int routing_config_version = 0;
    ServiceCapacity capacity;
};

class RoutingEngine {
public:
    static RoutingEngine& getInstance() {
        static RoutingEngine instance;
        return instance;
    }

    bool loadConfig(const std::string& path);
    
    ResolvedRoute resolveRoute(const std::string& destination_number, 
                               const std::string& ingress_stage,
                               const std::string& source_gateway);

    std::vector<ServiceRoute> getServices() {
        std::lock_guard<std::mutex> lock(mutex_);
        return services_;
    }

private:
    RoutingEngine() = default;
    ~RoutingEngine() = default;
    RoutingEngine(const RoutingEngine&) = delete;
    RoutingEngine& operator=(const RoutingEngine&) = delete;

    std::mutex mutex_;
    int version_ = 0;
    std::string on_unknown_entry_ = "static_fallback";
    std::vector<ServiceRoute> services_;
};
