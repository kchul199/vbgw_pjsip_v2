// HashiCorp Vault에서 secret을 읽어오는 간단한 클라이언트 인터페이스.
//
// 현재 프로젝트에서 넓게 쓰이고 있지는 않지만, 운영 환경에서 SIP/AI 자격증명을
// 코드나 .env 대신 secret manager에서 가져오려는 확장 지점으로 볼 수 있다.
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
