// AI TTS 오디오를 임시 보관하는 바이트 단위 FIFO 버퍼.
//
// VoicebotMediaPort의 송신 경로에서 hot path로 사용되므로,
// 구현은 단순하지만 wrap-around와 overwrite 정책을 명확히 이해하는 것이 중요하다.
#pragma once
#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

// Thread-safe RingBuffer (FIFO, Byte-level)
// write/read는 두 번의 memcpy로 wrap-around를 처리하여 CPU 낭비 최소화
//
// 현재 정책은 "가득 찼을 때 가장 오래된 데이터를 버리고 최신 데이터를 유지"하는 형태다.
// TTS 재생 버퍼 용도에서는 실시간성이 완전한 보존성보다 중요하므로 이 정책이 맞다.
class RingBuffer
{
public:
    // [Phase3-M3 Fix] size=0 생성 시 modulo-by-zero UB 방지
    explicit RingBuffer(size_t size) : buffer_(size), head_(0), tail_(0), count_(0)
    {
        if (size == 0)
            throw std::invalid_argument("RingBuffer: size must be > 0");
    }

    void write(const uint8_t* data, size_t len)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 버퍼 용량보다 큰 데이터 쓰기: 오래된 데이터를 덮어씀
        if (len > buffer_.size()) {
            data += (len - buffer_.size());
            len = buffer_.size();
            head_ = 0;
            tail_ = 0;
            count_ = 0;
        }
        // 오버플로우 시 head를 앞으로 밀어 가장 오래된 데이터 버림
        size_t overflow = (count_ + len > buffer_.size()) ? (count_ + len - buffer_.size()) : 0;
        if (overflow > 0) {
            head_ = (head_ + overflow) % buffer_.size();
            count_ -= overflow;
        }
        // Wrap-around 구간을 두 번의 memcpy로 처리 (SIMD 최적화 가능)
        size_t first = std::min(len, buffer_.size() - tail_);
        std::memcpy(buffer_.data() + tail_, data, first);
        if (len > first) {
            std::memcpy(buffer_.data(), data + first, len - first);
        }
        tail_ = (tail_ + len) % buffer_.size();
        count_ += len;
    }

    size_t read(uint8_t* data, size_t len)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t available = std::min(len, count_);
        if (available == 0)
            return 0;

        // Wrap-around 구간을 두 번의 memcpy로 처리
        size_t first = std::min(available, buffer_.size() - head_);
        std::memcpy(data, buffer_.data() + head_, first);
        if (available > first) {
            std::memcpy(data + first, buffer_.data(), available - first);
        }
        head_ = (head_ + available) % buffer_.size();
        count_ -= available;
        return available;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    std::vector<uint8_t> buffer_;
    size_t head_;
    size_t tail_;
    size_t count_;
    mutable std::mutex mutex_;
};
