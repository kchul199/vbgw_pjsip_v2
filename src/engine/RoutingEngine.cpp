#include "RoutingEngine.h"
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>

bool RoutingEngine::loadConfig(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        YAML::Node config = YAML::LoadFile(path);
        
        if (config["version"]) {
            version_ = config["version"].as<int>();
        }

        if (config["defaults"] && config["defaults"]["on_unknown_entry"]) {
            on_unknown_entry_ = config["defaults"]["on_unknown_entry"].as<std::string>();
        }

        std::vector<ServiceRoute> new_services;
        if (config["services"]) {
            for (const auto& service_node : config["services"]) {
                ServiceRoute service;
                service.name = service_node["name"].as<std::string>();
                service.enabled = service_node["enabled"].as<bool>();
                service.route_type = service_node["route_type"].as<std::string>();
                
                if (service_node["priority"]) {
                    service.priority = service_node["priority"].as<int>();
                }

                if (service_node["entry_numbers"]) {
                    service.entry_numbers = service_node["entry_numbers"].as<std::vector<std::string>>();
                }

                if (service_node["match"]) {
                    const auto& match_node = service_node["match"];
                    if (match_node["ingress_stages"]) {
                        service.match.ingress_stages = match_node["ingress_stages"].as<std::vector<std::string>>();
                    }
                    if (match_node["source_gateways"]) {
                        service.match.source_gateways = match_node["source_gateways"].as<std::vector<std::string>>();
                    }
                }

                if (service_node["capacity"]) {
                    const auto& cap_node = service_node["capacity"];
                    if (cap_node["backend"]) {
                        service.capacity.backend = cap_node["backend"].as<std::string>();
                    }
                    if (cap_node["max_concurrent"]) {
                        service.capacity.max_concurrent = cap_node["max_concurrent"].as<int>();
                    }
                    if (cap_node["allocator"]) {
                        service.capacity.allocator = cap_node["allocator"].as<std::string>();
                    }
                    if (cap_node["overflow_policy"]) {
                        service.capacity.overflow_policy = cap_node["overflow_policy"].as<std::string>();
                        service.capacity.overflow.policy = service.capacity.overflow_policy;
                    }
                    if (cap_node["transfer_target"]) {
                        service.capacity.transfer_target = cap_node["transfer_target"].as<std::string>();
                        service.capacity.overflow.target = service.capacity.transfer_target;
                    }

                    if (cap_node["extensions"]) {
                        service.capacity.extensions = cap_node["extensions"].as<std::vector<std::string>>();
                    }
                    if (cap_node["require_registered"]) {
                        service.capacity.require_registered = cap_node["require_registered"].as<bool>();
                    }

                    if (cap_node["overflow"]) {
                        const auto& ov_node = cap_node["overflow"];
                        if (ov_node["policy"]) {
                            service.capacity.overflow.policy = ov_node["policy"].as<std::string>();
                        }
                        if (ov_node["max_wait_seconds"]) {
                            service.capacity.overflow.max_wait_seconds = ov_node["max_wait_seconds"].as<int>();
                        }
                        if (ov_node["announcement"]) {
                            service.capacity.overflow.announcement = ov_node["announcement"].as<std::string>();
                        }
                        if (ov_node["on_timeout"]) {
                            service.capacity.overflow.on_timeout = ov_node["on_timeout"].as<std::string>();
                        }
                        if (ov_node["target"]) {
                            service.capacity.overflow.target = ov_node["target"].as<std::string>();
                        }
                        if (ov_node["announce_before_transfer"]) {
                            service.capacity.overflow.announce_before_transfer = ov_node["announce_before_transfer"].as<bool>();
                        }
                    }
                }
                new_services.push_back(service);
            }
        }

        services_ = std::move(new_services);
        spdlog::info("[RoutingEngine] Loaded config from {}. Version: {}, Services: {}", path, version_, services_.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[RoutingEngine] Failed to load config from {}: {}", path, e.what());
        return false;
    }
}

ResolvedRoute RoutingEngine::resolveRoute(const std::string& destination_number, 
                                           const std::string& ingress_stage,
                                           const std::string& source_gateway) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& service : services_) {
        if (!service.enabled) continue;

        // Check if destination_number is in entry_numbers
        auto it = std::find(service.entry_numbers.begin(), service.entry_numbers.end(), destination_number);
        if (it == service.entry_numbers.end()) continue;

        // Check match rules
        bool stage_matched = service.match.ingress_stages.empty();
        if (!stage_matched) {
            stage_matched = (std::find(service.match.ingress_stages.begin(), service.match.ingress_stages.end(), ingress_stage) != service.match.ingress_stages.end());
        }

        bool gateway_matched = service.match.source_gateways.empty();
        if (!gateway_matched) {
            gateway_matched = (std::find(service.match.source_gateways.begin(), service.match.source_gateways.end(), source_gateway) != service.match.source_gateways.end());
        }

        if (stage_matched && gateway_matched) {
            ResolvedRoute route;
            route.matched = true;
            route.service_name = service.name;
            route.route_type = service.route_type;
            route.entry_number = destination_number;
            route.routing_config_version = version_;
            route.capacity = service.capacity;
            return route;
        }
    }

    return ResolvedRoute{false, "", "", "", version_};
}
