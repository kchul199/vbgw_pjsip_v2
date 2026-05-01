// Redis 기반 분산 슬롯 lease의 실제 구현.
//
// 이 파일의 핵심은 "로컬 프로세스 카운트"가 아니라
// "여러 VBGW 인스턴스가 하나의 서비스 용량을 공유할 때 중복 배정을 막는 방식"이다.
// 그래서 lease/release/heartbeat/getActiveCount 모두 Redis key를 기준으로 동작한다.
#include "CapacityManager.h"
#include <spdlog/spdlog.h>
#include <uuid/uuid.h> // 고유 slot_id 생성을 위해 uuid 사용 (시스템 라이브러리)

#include <chrono>

namespace {

constexpr long long kLeaseTtlSeconds = 30;

long long leaseNowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}

// Lua Script: Max Concurrent 체크 후 슬롯 할당 및 TTL 설정
//
// ZSET score를 expires_at으로 사용하면, 별도 sweep 없이도 stale slot을 주기적으로
// 제거할 수 있고 active count 계산도 한 구조 위에서 수행할 수 있다.
// KEYS[1]: vbgw:slots:{service_name} (ZSET, member=slot_id, score=expires_at_epoch_sec)
// KEYS[2]: vbgw:lease:{slot_id} (STRING)
// ARGV[1]: slot_id
// ARGV[2]: max_concurrent
// ARGV[3]: now_epoch_sec
// ARGV[4]: ttl_seconds
// ARGV[5]: service_name
const std::string LEASE_LUA = R"(
    local now_sec = tonumber(ARGV[3])
    local ttl_sec = tonumber(ARGV[4])
    local expires_at = now_sec + ttl_sec
    redis.call('ZREMRANGEBYSCORE', KEYS[1], '-inf', now_sec)
    local count = redis.call('ZCARD', KEYS[1])
    local max_cap = tonumber(ARGV[2])
    if max_cap > 0 and count >= max_cap then
        return 0
    end
    redis.call('ZADD', KEYS[1], expires_at, ARGV[1])
    redis.call('SETEX', KEYS[2], ttl_sec, ARGV[5])
    return 1
)";

bool CapacityManager::init(const std::string& redis_addr) {
    try {
        sw::redis::ConnectionOptions opts;
        // 간단한 파싱: tcp://127.0.0.1:6379 -> host/port 추출
        // 실제로는 정교한 파싱이 필요하지만 우선 redis-plus-plus의 URI 지원 활용
        redis_ = std::make_unique<sw::redis::Redis>(redis_addr);
        
        // 연결 테스트
        redis_->ping();
        
        spdlog::info("[CapacityManager] Connected to Redis at {}", redis_addr);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[CapacityManager] Failed to connect to Redis: {}", e.what());
        return false;
    }
}

SlotLease CapacityManager::leaseSlot(const std::string& service_name, int max_concurrent) {
    if (!redis_) {
        // 현재 정책은 Redis가 분산 용량 제어의 전제라는 가정에 가깝다.
        // 운영 요구가 fail-open이면 바로 이 분기부터 정책을 바꿔야 한다.
        spdlog::warn("[CapacityManager] Redis not connected. Fallback to fail-open or reject.");
        return {false, ""}; 
    }

    // UUID 생성
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    std::string slot_id = uuid_str;

    std::string slots_key = "vbgw:slots:" + service_name;
    std::string lease_key = "vbgw:lease:" + slot_id;

    try {
        const auto now_sec = leaseNowSeconds();
        auto result = redis_->command<long long>(
            "EVAL", LEASE_LUA, 2, slots_key.c_str(), lease_key.c_str(),
            slot_id.c_str(), std::to_string(max_concurrent).c_str(),
            std::to_string(now_sec).c_str(), std::to_string(kLeaseTtlSeconds).c_str(),
            service_name.c_str());

        if (result == 1) {
            spdlog::info("[CapacityManager] Leased slot {} for service {}", slot_id, service_name);
            return {true, slot_id};
        } else {
            spdlog::warn("[CapacityManager] Overflow for service {}. Max: {}", service_name, max_concurrent);
            return {false, ""};
        }
    } catch (const std::exception& e) {
        spdlog::error("[CapacityManager] Redis error during lease: {}", e.what());
        return {false, ""};
    }
}

void CapacityManager::releaseSlot(const std::string& service_name, const std::string& slot_id) {
    if (!redis_ || service_name.empty() || slot_id.empty()) return;

    try {
        std::string slots_key = "vbgw:slots:" + service_name;
        std::string lease_key = "vbgw:lease:" + slot_id;

        auto pipeline = redis_->pipeline();
        pipeline.zrem(slots_key, slot_id)
                .del(lease_key)
                .exec();

        spdlog::info("[CapacityManager] Released slot {} for service {}", slot_id, service_name);
    } catch (const std::exception& e) {
        spdlog::error("[CapacityManager] Redis error during release: {}", e.what());
    }
}

void CapacityManager::refreshLease(const std::string& slot_id) {
    if (!redis_ || slot_id.empty()) return;

    try {
        // heartbeat는 lease key 갱신과 ZSET score 갱신을 동시에 맞춰,
        // active count와 실제 TTL 관점을 일관되게 유지한다.
        std::string lease_key = "vbgw:lease:" + slot_id;
        auto service_name = redis_->get(lease_key);
        if (!service_name) {
            return;
        }

        std::string slots_key = "vbgw:slots:" + *service_name;
        const auto now_sec = leaseNowSeconds();
        const auto expires_at = now_sec + kLeaseTtlSeconds;

        auto pipeline = redis_->pipeline();
        pipeline.expire(lease_key, kLeaseTtlSeconds)
                .zadd(slots_key, slot_id, expires_at)
                .exec();
    } catch (...) {
        // Heartbeat 실패는 로그만 남김
    }
}

int CapacityManager::getActiveCount(const std::string& service_name) {
    if (!redis_) return 0;
    try {
        const std::string slots_key = "vbgw:slots:" + service_name;
        const auto now_sec = leaseNowSeconds();
        redis_->command<long long>("ZREMRANGEBYSCORE", slots_key.c_str(), "-inf",
                                   std::to_string(now_sec).c_str());
        return static_cast<int>(redis_->zcard(slots_key));
    } catch (...) {
        return 0;
    }
}
