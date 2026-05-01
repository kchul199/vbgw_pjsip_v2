#include "vault_client.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace vbgw {
namespace utils {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

VaultClient::VaultClient(const std::string& vault_addr, const std::string& vault_token)
    : vault_addr_(vault_addr), vault_token_(vault_token) {
}

std::optional<std::string> VaultClient::GetSecret(const std::string& secret_path, const std::string& key) {
    if (vault_addr_.empty() || vault_token_.empty()) {
        spdlog::warn("Vault address or token is empty");
        return std::nullopt;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        spdlog::error("Failed to initialize cURL");
        return std::nullopt;
    }

    std::string url = vault_addr_ + "/v1/" + secret_path;
    std::string read_buffer;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("X-Vault-Token: " + vault_token_).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    
    // Ignore SSL verification for local testing, should be enabled in prod
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        spdlog::error("Vault cURL request failed: {}", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        spdlog::error("Vault returned HTTP code: {}", http_code);
        return std::nullopt;
    }

    try {
        auto json_res = nlohmann::json::parse(read_buffer);
        if (json_res.contains("data") && json_res["data"].contains("data") && json_res["data"]["data"].contains(key)) {
            return json_res["data"]["data"][key].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse Vault response JSON: {}", e.what());
    }

    return std::nullopt;
}

} // namespace utils
} // namespace vbgw
