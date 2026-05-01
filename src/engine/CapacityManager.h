// Redis 기반 분산 용량 제어 인터페이스.
//
// 이 파일은 "같은 서비스에 대해 여러 VBGW 인스턴스가 동시에 몇 콜까지 받을 수 있는가"
// 를 조정하는 레이어다. 로컬 프로세스 상한(SessionManager)과는 다른 책임이다.
#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <sw/redis++/redis++.h>

struct SlotLease {
    bool success = false;
    std::string slot_id;
};

/**
 * @brief 분산 슬롯 관리자 (Redis 기반)
 * 
 * 다중 노드 환경에서 서비스별 동시 처리량을 보장하기 위해
 * Redis Lua 스크립트를 사용하여 원자적(Atomic) 슬롯 할당 및 반환을 수행합니다.
 */
class CapacityManager {
public:
    static CapacityManager& getInstance() {
        static CapacityManager instance;
        return instance;
    }

    // Redis 연결 초기화
    bool init(const std::string& redis_addr);

    // 슬롯 할당 시도 (Distributed Atomic Lease)
    SlotLease leaseSlot(const std::string& service_name, int max_concurrent);
    
    // 슬롯 반환
    void releaseSlot(const std::string& service_name, const std::string& slot_id);

    // 슬롯 유효시간 연장 (Heartbeat)
    void refreshLease(const std::string& slot_id);

    // 현재 활성 슬롯 수 조회 (Redis 조회)
    int getActiveCount(const std::string& service_name);

private:
    CapacityManager() = default;
    ~CapacityManager() = default;
    CapacityManager(const CapacityManager&) = delete;
    CapacityManager& operator=(const CapacityManager&) = delete;

    std::unique_ptr<sw::redis::Redis> redis_;
    std::mutex mutex_;
    
    // 할당용 Lua 스크립트 SHA 또는 소스 보관
    std::string lease_script_sha_;
};
