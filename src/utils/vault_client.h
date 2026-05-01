#pragma once

#include <string>
#include <optional>

namespace vbgw {
namespace utils {

class VaultClient {
public:
    VaultClient(const std::string& vault_addr, const std::string& vault_token);
    ~VaultClient() = default;

    // Fetch a secret from Vault KV v2 engine
    // secret_path: e.g., "secret/data/vbgw/prod"
    // key: e.g., "sip_password"
    std::optional<std::string> GetSecret(const std::string& secret_path, const std::string& key);

private:
    std::string vault_addr_;
    std::string vault_token_;
};

} // namespace utils
} // namespace vbgw
