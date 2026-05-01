// RTP <-> AI 오디오 경계에서 실제 프레임을 주고받는 AudioMediaPort 인터페이스.
//
// 쉽게 말해 이 클래스가 "전화망 오디오"를 "AI용 PCM 스트림"으로 바꾸고,
// 반대로 "AI가 만든 TTS PCM"을 "전화망 재생 버퍼"로 돌려준다.
#pragma once
#include <pjsua2.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

class VoicebotAiClient;
class RingBuffer;
class SileroVad;
class SpeexDsp;

// 통화당 1개 생성되는 사용자 정의 AudioMediaPort.
//
// 읽을 때 핵심:
// 1. onFrameReceived(): 고객 음성을 받아 전처리/VAD/AI 전송
// 2. onFrameRequested(): AI TTS 버퍼를 읽어 PBX 쪽으로 송신
// 3. quiesce/shutdown/retire(): PJSIP 비동기 unregister와 객체 수명 분리
class VoicebotMediaPort : public pj::AudioMediaPort
{
public:
    VoicebotMediaPort();
    void setAiClient(std::shared_ptr<VoicebotAiClient> client);
    virtual ~VoicebotMediaPort();  // unique_ptr 소멸자는 cpp에서 완전 타입 필요
    void quiesce();
    void shutdown();
    static void retire(std::unique_ptr<VoicebotMediaPort> port);

    // 수신 (Rx): PBX에서 보낸 사용자(고객) 음성이 들어오는 곳 -> STT 스트리밍용
    virtual void onFrameReceived(pj::MediaFrame& frame) override;

    // 송신 (Tx): PBX로 보낼 봇(TTS) 음성을 요청받는 곳 -> TTS 재생 버퍼
    virtual void onFrameRequested(pj::MediaFrame& frame) override;

    // AI 엔진에서 내려온 TTS 오디오를 재생 버퍼에 쓰기
    void writeTtsAudio(const uint8_t* data, size_t len);

    // VAD 감지 등 AI 서버의 말끊기 응답 시 재생 버퍼 잔량 강제 폐기(Flush)
    void clearTtsAudio();

    // 강제 VAD 초기화 (Barge-in 시 상태 소거용)
    void resetVad();

    // VAD speech-start 이벤트 콜백 (false->true edge)
    void setVadSpeechStartCallback(std::function<void()> cb);
    
    // [Step 5] VAD speech-end 이벤트 콜백 (true->false edge)
    void setVadSpeechEndCallback(std::function<void()> cb);

    // [P2-2 Fix] AI 포워딩 일시정지 설정 (Bridge 모드 등에서 AI 개입 차단)
    void setAiPaused(bool paused);

private:
    std::unique_ptr<RingBuffer> tts_buffer_;  // RAII
    std::shared_ptr<VoicebotAiClient> ai_client_;
    std::unique_ptr<SileroVad> vad_;       // RAII
    std::unique_ptr<SpeexDsp> speex_dsp_;  // RAII — Denoise + AGC

    std::function<void()> on_vad_speech_start_;
    std::function<void()> on_vad_speech_end_;
    bool last_vad_state_ = false;

    // [P2-2 Fix] AI 포워딩 일시정지 플래그
    std::atomic<bool> ai_paused_{false};
    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> unregister_requested_{false};

    // Thread safety for ai_client_
    std::mutex client_mutex_;
};
