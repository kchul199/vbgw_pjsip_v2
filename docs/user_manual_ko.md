# VoiceBot Gateway 상세 사용자·개발자 매뉴얼 (한국어)

이 문서는 VBGW를 처음 받는 사람도 이 문서만 보고 다음 일을 할 수 있도록 작성한 기준 문서입니다.

1. 프로젝트를 빌드하고 실행한다.
2. 로컬에서 SIP/gRPC/오디오 흐름을 검증한다.
3. 설정 파일과 환경변수를 이해하고 수정한다.
4. 어느 소스 파일을 수정해야 하는지 판단한다.
5. 신규 기능, 버그 수정, 운영용 API 변경을 안전하게 반영한다.

이 문서의 기본 전제는 “문서만 읽고도 소스를 수정할 수 있어야 한다”입니다. 초보자용 설치 가이드 수준에서 멈추지 않고, 실제 코드 구조와 수정 절차까지 포함합니다.

---

## 1. 이 프로젝트가 무엇을 하는지 한 문장으로 이해하기

VBGW는 **SIP/RTP 통화망과 AI 엔진(STT/LLM/TTS)을 연결하는 실시간 음성 게이트웨이**입니다.

입력:

1. PBX/SBC 또는 테스트 도구(SIPp)가 SIP INVITE와 RTP 음성을 보냅니다.
2. VBGW는 RTP를 받아 PCM으로 정리하고 VAD를 거쳐 AI 엔진으로 gRPC 스트리밍합니다.
3. AI 엔진은 STT 결과, TTS 오디오, 턴 종료 이벤트를 반환합니다.
4. VBGW는 TTS 오디오를 다시 RTP로 송신합니다.

핵심은 “통화 제어”와 “미디어 중계”를 동시에 한다는 점입니다. 따라서 단순 SIP 앱이 아니라 다음 세 가지 축을 함께 이해해야 합니다.

1. SIP/PJSIP 계층
2. 오디오/VAD/gRPC 계층
3. 운영 API/모니터링/분산 용량 제어 계층

---

## 2. 문서만으로 소스를 수정하려면 먼저 잡아야 하는 큰 그림

### 2.1 런타임 데이터 흐름

```text
SIP INVITE
  -> VoicebotEndpoint
  -> VoicebotAccount
  -> RoutingEngine + CapacityManager
  -> VoicebotCall
  -> VoicebotMediaPort
  -> SpeexDSP + Silero VAD
  -> VoicebotAiClient (gRPC bi-directional stream)
  -> TTS 수신
  -> RingBuffer
  -> RTP 송신
```

### 2.2 컨트롤 플레인 흐름

```text
운영자/백엔드
  -> HTTP Admin API
  -> SessionManager / VoicebotCall
  -> DTMF / Transfer / Recording / Bridge / Stats
```

### 2.3 분산 용량 제어 흐름

```text
신규 인입호
  -> RoutingEngine이 서비스 결정
  -> CapacityManager가 Redis lease 확보
  -> SessionManager가 로컬 프로세스 동시호 수 확인
  -> VoicebotCall 생성
```

이 세 흐름을 따로 보지 말고, “인입호가 들어온 뒤 어떤 계층이 어떤 순서로 개입하는가”로 보시면 코드가 훨씬 빨리 읽힙니다.

---

## 3. 먼저 열어야 하는 파일 순서

처음 코드를 읽는 사람에게 가장 추천하는 순서는 아래와 같습니다.

1. `README.md`
2. `src/main.cpp`
3. `src/utils/AppConfig.h`
4. `src/engine/VoicebotEndpoint.cpp`
5. `src/engine/VoicebotAccount.cpp`
6. `src/engine/VoicebotCall.cpp`
7. `src/engine/VoicebotMediaPort.cpp`
8. `src/ai/VoicebotAiClient.cpp`
9. `src/api/HttpServer.cpp`
10. `src/engine/SessionManager.cpp`
11. `src/engine/CapacityManager.cpp`
12. `config/routing.yaml`

이 순서는 실제 실행 순서와 거의 같습니다.

### 3.1 파일별 역할 요약

| 파일 | 역할 | 언제 수정하는가 |
|------|------|----------------|
| `src/main.cpp` | 전체 부팅 순서, 계정/트랜스포트 구성, HTTP 서버 시작 | 신규 전역 설정, 부트 순서, 프로세스 시작/종료 정책 변경 |
| `src/utils/AppConfig.h` | 환경변수 정의와 기본값, production 보안 정책 | 새 환경변수 추가, 기본값 변경, 운영 정책 강화 |
| `src/engine/VoicebotEndpoint.*` | PJSIP endpoint 초기화와 transport 구성 | UDP/TCP/TLS, STUN/TURN, 콜 수용 상한 진단 |
| `src/engine/VoicebotAccount.*` | SIP 계정, 인입/발신 콜 생성, 라우팅 진입점 | 인입호 정책, 302 redirect, outbound call, PBX failover |
| `src/engine/VoicebotCall.*` | 통화 오케스트레이션 중심 | transfer, recording, bridge, CDR, 턴 수 정책 |
| `src/engine/VoicebotMediaPort.*` | RTP 수신/송신, AI 전송, VAD, TTS buffer | 오디오 처리, AI pause, barge-in, 포트 수명주기 |
| `src/ai/VoicebotAiClient.*` | AI gRPC bi-dir streaming | proto 변경, 재연결 정책, STT/TTS 처리 |
| `src/api/HttpServer.*` | 운영 API, health/metrics | 신규 관리 API, 인증/레이트리밋, 운영 JSON 응답 |
| `src/engine/RoutingEngine.*` | 번호/게이트웨이 기반 서비스 라우팅 | 서비스 매핑 정책, overflow 동작 |
| `src/engine/CapacityManager.*` | Redis 기반 분산 슬롯 lease | 멀티 인스턴스 용량 제어, Redis 동작 |
| `src/engine/SessionManager.*` | 프로세스 내 활성 콜 수명주기 관리 | 동시호 제한, stale session 정리, zombie retention |
| `src/ivr/IvrManager.*` | DTMF 기반 상태 전이 | 메뉴 구조, AI chat / transfer / disconnect 정책 |
| `config/routing.yaml` | 서비스/대표번호/overflow 정책 | 운영 서비스 추가, 대표번호 추가, queue/fallback 정책 |

---

## 4. 개발 환경 준비

### 4.1 필수 의존성

macOS 기준으로 아래가 필요합니다.

1. `cmake`
2. `ninja`
3. `pkg-config`
4. `openssl@3`
5. `pjproject`
6. `grpc`
7. `protobuf`
8. `boost`
9. `onnxruntime`
10. `speexdsp`
11. `spdlog`
12. `prometheus-cpp`
13. `opentelemetry-cpp`
14. `nlohmann-json`
15. `redis-plus-plus`, `hiredis`
16. `sipp` 테스트용

예시:

```bash
brew install cmake ninja pkg-config openssl@3 pjproject grpc protobuf boost \
  onnxruntime speexdsp spdlog prometheus-cpp opentelemetry-cpp nlohmann-json \
  redis-plus-plus hiredis sipp
```

### 4.2 고동시성 테스트를 할 예정이라면 반드시 알아야 할 점

Homebrew 기본 `pjproject`는 컴파일타임 상한이 낮을 수 있습니다. 특히 `PJSUA_MAX_CALLS`가 낮으면 `MAX_CONCURRENT_CALLS`를 크게 잡아도 실제 수용량이 올라가지 않습니다.

이 저장소에는 이를 해결하기 위한 스크립트가 이미 포함되어 있습니다.

1. `scripts/build_local_pjproject.sh`
2. `scripts/configure_with_local_pjproject.sh`

고동시성 빌드 절차:

```bash
./scripts/build_local_pjproject.sh
BUILD_DIR=build-local ./scripts/configure_with_local_pjproject.sh -DCMAKE_BUILD_TYPE=Release
cmake --build build-local
```

기본값:

1. `PJSUA_MAX_CALLS=256`
2. `PJSUA_MAX_CONF_PORTS=1024`
3. `PJSIP_MAX_TSX_COUNT=4095`
4. `PJ_IOQUEUE_MAX_HANDLES=1024`

즉, “부하 테스트를 할 계획이 있다면” 시스템 패키지 PJPROJECT만 믿지 말고 저장소 내 스크립트 경로를 우선 검토하세요.

---

## 5. 최초 빌드와 실행

### 5.1 기본 빌드

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

### 5.2 로컬 테스트용 최소 환경변수

```bash
export AI_ENGINE_ADDR=127.0.0.1:50051
export GRPC_USE_TLS=0
export SIP_PORT=5060
export HTTP_PORT=8080
export MAX_CONCURRENT_CALLS=100
export PJSIP_NULL_AUDIO=1
export LOG_LEVEL=debug
export PJSIP_LOG_LEVEL=4
```

`PJSIP_NULL_AUDIO=1`은 로컬 자동화 테스트에서는 유용하지만, 운영 프로파일에서는 허용되지 않습니다.

### 5.3 Mock AI 서버 실행

```bash
cd src/emulator
python3 mock_server.py
```

프로젝트에는 `src/emulator/venv/bin/python3`를 우선 사용하는 테스트 스크립트들이 있으므로, emulator 전용 가상환경을 맞춰두면 재현성이 좋아집니다.

### 5.4 VBGW 실행

```bash
./build/vbgw
```

### 5.5 기동 직후 확인해야 할 것

1. `/live`가 200인지
2. `/ready`가 200 또는 503인지
3. `/health` JSON에 SIP/gRPC 상태가 어떻게 나오는지
4. startup 로그에 `effective` / `compile_time_cap` 관련 경고가 있는지

예시:

```bash
curl -s http://127.0.0.1:8080/live
curl -s http://127.0.0.1:8080/ready
curl -s http://127.0.0.1:8080/health
```

---

## 6. 환경변수 체계 이해하기

이 프로젝트를 수정하려면 환경변수가 어디에서 읽히는지 반드시 알아야 합니다. 모든 중심 설정은 `src/utils/AppConfig.h`에 있습니다.

### 6.1 SIP/보안/트랜스포트

| 변수 | 기본값 | 의미 |
|------|--------|------|
| `SIP_PORT` | `5060` | SIP 리슨 포트 |
| `SIP_USE_TLS` | `false` | TLS 사용 여부의 상위 스위치 |
| `SRTP_ENABLE` | `false` | SRTP 활성화 |
| `SRTP_MANDATORY` | `false` | SRTP 강제 여부 |
| `SIP_TLS_CERT_FILE` | 빈 값 | SIP TLS 인증서 |
| `SIP_TLS_PRIVKEY_FILE` | 빈 값 | SIP TLS 개인키 |
| `SIP_TLS_CA_FILE` | 빈 값 | SIP TLS CA |
| `SIP_TRANSPORT_UDP_ENABLE` | `true` | UDP transport |
| `SIP_TRANSPORT_TCP_ENABLE` | `false` | TCP transport |
| `SIP_TRANSPORT_TLS_ENABLE` | `SIP_USE_TLS` | TLS transport |
| `SIP_TRANSPORT_PREFERRED` | `tls` 또는 `udp` | 우선 transport |

수정 기준:

1. transport를 더 열거나 닫는 정책은 `VoicebotEndpoint`까지 같이 봅니다.
2. production에서 TLS/SRTP를 약화시키려는 변경은 `validateRuntimeSecurityPolicy()`와 충돌할 수 있습니다.

### 6.2 NAT/STUN/TURN

| 변수 | 기본값 | 의미 |
|------|--------|------|
| `SIP_STUN_SERVER` | 빈 값 | STUN 서버 |
| `SIP_STUN_SIP_ENABLE` | `false` | SIP STUN 사용 |
| `SIP_STUN_MEDIA_ENABLE` | `false` | media STUN 사용 |
| `SIP_NAT_CONTACT_REWRITE_ENABLE` | `true` | Contact rewrite |
| `SIP_NAT_CONTACT_REWRITE_MODE` | `1` | rewrite 모드 |
| `SIP_NAT_VIA_REWRITE_ENABLE` | `true` | Via rewrite |
| `SIP_NAT_SDP_REWRITE_ENABLE` | `false` | SDP rewrite |
| `SIP_NAT_SIP_OUTBOUND_ENABLE` | `true` | SIP outbound |
| `SIP_UDP_KEEPALIVE_INTERVAL_SECS` | `15` | UDP keepalive 주기 |
| `SIP_ICE_ENABLE` | `false` | ICE 사용 |
| `SIP_TURN_ENABLE` | `false` | TURN 사용 |
| `SIP_TURN_SERVER` | 빈 값 | TURN 서버 |
| `SIP_TURN_USERNAME` | 빈 값 | TURN 계정 |
| `SIP_TURN_PASSWORD` | 빈 값 | TURN 비밀번호 |

운영 팁:

1. TURN을 켜면 production 검증에서 서버/계정/비밀번호가 모두 필요합니다.
2. NAT rewrite 문제는 SIP 메시지 레벨과 RTP 경로 레벨을 나눠서 봐야 합니다.

### 6.3 SIP 세션 제어

| 변수 | 기본값 | 의미 |
|------|--------|------|
| `SIP_PRACK_MODE` | `off` | `off/optional/mandatory` |
| `SIP_SESSION_TIMER_MODE` | `optional` | `inactive/optional/required/always` |
| `SIP_TIMER_MIN_SE_SECS` | `90` | Min-SE |
| `SIP_TIMER_SESS_EXPIRES_SECS` | `1800` | Session-Expires |
| `SIP_FOLLOW_REDIRECT` | `true` | redirect follow |
| `SIP_REDIRECT_REPLACE_TO` | `false` | redirect 시 To 헤더 교체 |
| `SIP_ACCEPT_REFER` | `true` | REFER 수용 |
| `SIP_ACCEPT_REPLACES` | `true` | Replaces 수용 |

### 6.4 PBX 계정

Main과 Standby를 각각 둘 수 있습니다.

| 변수 | 의미 |
|------|------|
| `PBX_MAIN_URI` | 메인 SIP registrar/proxy URI |
| `PBX_MAIN_ID_URI` | 메인 계정 ID URI |
| `PBX_MAIN_USERNAME` | 메인 계정 ID |
| `PBX_MAIN_PASSWORD` | 메인 계정 비밀번호 |
| `PBX_MAIN_REGISTER_ENABLE` | 메인 계정 등록 여부 |
| `PBX_STANDBY_URI` | 스탠바이 SIP URI |
| `PBX_STANDBY_ID_URI` | 스탠바이 계정 ID URI |
| `PBX_STANDBY_USERNAME` | 스탠바이 계정 ID |
| `PBX_STANDBY_PASSWORD` | 스탠바이 계정 비밀번호 |
| `PBX_STANDBY_REGISTER_ENABLE` | 스탠바이 등록 여부 |

레거시 fallback:

1. `PBX_URI`
2. `PBX_ID_URI`
3. `PBX_USERNAME`
4. `PBX_PASSWORD`
5. `SIP_REGISTER_ENABLE`

주의:

1. `AccountManager`의 probe는 현재 등록 상태 중심이며, 완전한 SIP OPTIONS 기반 active probe는 아닙니다.
2. README의 failover 설명보다 실제 구현은 단순합니다. 무조건적인 무중단 active-active로 이해하면 안 됩니다.

### 6.5 AI/gRPC

| 변수 | 기본값 | 의미 |
|------|--------|------|
| `AI_ENGINE_ADDR` | `localhost:50051` | AI 서버 주소 |
| `GRPC_USE_TLS` | `false` | gRPC TLS |
| `GRPC_TLS_CA_CERT` | 빈 값 | CA cert |
| `GRPC_TLS_CLIENT_CERT` | 빈 값 | client cert |
| `GRPC_TLS_CLIENT_KEY` | 빈 값 | client key |
| `GRPC_STREAM_DEADLINE_SECS` | `86400` | 스트림 최대 수명 |
| `GRPC_MAX_RECONNECT_RETRIES` | `5` | 재연결 최대 횟수 |
| `GRPC_MAX_BACKOFF_MS` | `4000` | 지수 백오프 상한 |

이 계층을 수정할 때 기억할 점:

1. gRPC 채널은 `AppConfig::getGrpcChannel()`에서 싱글톤으로 공유됩니다.
2. `VoicebotAiClient`는 read/write 스레드가 분리되어 있습니다.
3. 재연결 시 큐를 비워 오래된 오디오가 AI에 전달되지 않게 되어 있습니다.

### 6.6 미디어/VAD

| 변수 | 기본값 | 의미 |
|------|--------|------|
| `SILERO_VAD_MODEL_PATH` | `models/silero_vad.onnx` | ONNX VAD 모델 |
| `TTS_BUFFER_SECS` | `5` | TTS ring buffer 길이 |
| `SPEEX_DENOISE_ENABLE` | `true` | denoise |
| `SPEEX_AGC_ENABLE` | `true` | AGC |
| `SPEEX_AGC_LEVEL` | `16000` | AGC target |

### 6.7 RTP/JitterBuffer

| 변수 | 기본값 | 의미 |
|------|--------|------|
| `RTP_PORT_MIN` | `16000` | RTP 시작 포트 |
| `RTP_PORT_MAX` | `20000` | RTP 끝 포트 |
| `RTP_STREAM_KEEPALIVE_ENABLE` | `true` | keepalive |
| `RTP_RTCP_MUX_ENABLE` | `false` | RTCP mux |
| `RTP_RTCP_XR_ENABLE` | `true` | RTCP XR |
| `RTP_RTCP_FB_NACK_ENABLE` | `false` | RTCP feedback nack |
| `JB_INIT_MS` | `100` | jitter buffer 초기 지연 |
| `JB_MIN_PRE_MS` | `60` | jitter buffer 최소 prefetch |
| `JB_MAX_PRE_MS` | `240` | jitter buffer 최대 prefetch |
| `JB_MAX_MS` | `500` | jitter buffer 최대 길이 |

### 6.8 운영/API/레코딩/웹훅/Redis

| 변수 | 기본값 | 의미 |
|------|--------|------|
| `MAX_CONCURRENT_CALLS` | `100` | 프로세스 로컬 동시호 상한 |
| `ANSWER_DELAY_MS` | `200` | 인입호 200 OK 지연 |
| `HTTP_PORT` | `8080` | HTTP Admin API 포트 |
| `ADMIN_API_KEY` | `changeme-admin-key` | 관리자 API 키 |
| `ADMIN_API_RATE_LIMIT_RPS` | `20` | outbound API rate limit |
| `ADMIN_API_RATE_LIMIT_BURST` | `40` | outbound API burst |
| `ADMIN_API_MAX_BODY_BYTES` | `8192` | 바디 제한 |
| `ADMIN_API_MAX_HEADER_BYTES` | `16384` | 헤더 제한 |
| `VBGW_PROFILE` | `dev` | `dev/prod/production` 등 |
| `CALL_RECORDING_ENABLE` | `false` | 자동 녹취 |
| `CALL_RECORDING_DIR` | `recordings` | 녹취 디렉토리 |
| `CALL_RECORDING_MAX_DAYS` | `30` | 녹취 최대 보관 일수 |
| `CALL_RECORDING_MAX_MB` | `1024` | 녹취 최대 용량 |
| `CDR_WEBHOOK_ENABLE` | `false` | CDR webhook 사용 |
| `CDR_WEBHOOK_URL` | 빈 값 | webhook 주소 |
| `REDIS_ADDR` | `tcp://127.0.0.1:6379` | Redis 주소 |

주의:

1. `REDIS_URL`은 레거시 fallback일 뿐이며, 새 구성은 `REDIS_ADDR`를 기준으로 해야 합니다.
2. production 프로파일은 `ADMIN_API_KEY`, TLS, SRTP, null audio 비활성화 등을 강하게 검증합니다.

---

## 7. 라우팅 설정 파일 읽는 법

라우팅은 `config/routing.yaml`에서 정의합니다.

핵심 구조:

```yaml
version: 2

defaults:
  on_unknown_entry: static_fallback

services:
  - name: bot-main
    enabled: true
    route_type: ai
    priority: 100
    entry_numbers: ["1000", "5551212"]
    match:
      ingress_stages: ["default-policy"]
      source_gateways: ["pbx-main", "pbx-standby"]
    capacity:
      backend: logical
      max_concurrent: 10
      allocator: round_robin
      overflow:
        policy: queue
        max_wait_seconds: 20
        announcement: "ivr/queue-wait.wav"
        on_timeout: busy
```

### 7.1 어떤 경우에 이 파일만 수정하면 되는가

1. 대표번호를 추가할 때
2. 서비스 이름을 바꿀 때
3. 특정 게이트웨이만 허용할 때
4. overflow를 busy에서 transfer로 바꿀 때
5. 서비스별 동시호 제한을 조정할 때

### 7.2 어떤 경우에 코드까지 같이 수정해야 하는가

1. `capacity.backend`의 새 타입을 추가할 때
2. overflow 정책에 새 동작을 추가할 때
3. `match` 문법 자체를 확장할 때
4. queue announcement 재생 방식이나 timeout 후 동작을 바꿀 때

### 7.3 현재 구현 기준 overflow 이해

현재 인입 시점은 대략 이렇게 동작합니다.

1. `VoicebotAccount::onIncomingCall()`
2. `RoutingEngine::resolveRoute()`
3. `CapacityManager::leaseSlot()`
4. overflow이면
   - direct transfer 정책 + target 있으면 302 redirect
   - 아니면 busy 처리
5. lease 성공이면 `VoicebotCall` 생성

즉, queue/fallback 문구가 YAML에 있어도 실제로 어떤 동작이 이미 구현되었는지는 `VoicebotAccount`와 `VoicebotCall`에서 확인해야 합니다.

---

## 8. 코드 수정 전에 알아야 하는 스레드 모델

이 프로젝트는 멀티스레드 시스템입니다.

주요 실행 주체:

1. PJSIP 내부 스레드
2. HTTP worker 스레드
3. gRPC write worker
4. gRPC read worker
5. lease heartbeat timer
6. PBX probing thread

### 8.1 수정 시 가장 많이 깨지는 부분

1. 락을 잡은 채 외부 콜백 호출
2. 이미 해제된 media port 접근
3. PJSIP 콜백 안에서 장시간 블로킹
4. call/session ID 혼용
5. session이 종료된 뒤 async callback이 다시 진입

### 8.2 반드시 지켜야 할 원칙

1. “락 안에서 외부 코드 호출 금지”
2. PJSIP 객체와 C++ 객체의 수명주기를 분리해서 생각하기
3. call slot ID와 session ID를 다른 개념으로 보기
4. 운영 API는 인증, 감사 로그, 유효성 검증을 같이 추가하기
5. 새 기능을 넣으면 metrics와 test도 함께 넣기

---

## 9. 실제 소스 변경 시나리오별 가이드

이 절은 가장 실무적인 부분입니다. “어떤 요구가 들어왔을 때 어느 파일을 어떻게 바꿔야 하는가”를 바로 찾을 수 있게 정리했습니다.

### 9.1 대표번호를 추가하고 다른 서비스로 라우팅하고 싶다

수정 순서:

1. `config/routing.yaml`
2. 필요 시 `src/engine/RoutingEngine.*`
3. `/api/v1/services` 결과 확인
4. SIPp 또는 실제 INVITE로 검증

코드 수정이 필요 없는 경우:

1. 기존 문법 안에서 entry number만 추가
2. `max_concurrent`만 조정
3. overflow target만 변경

### 9.2 인입호 수락 정책을 바꾸고 싶다

주요 파일:

1. `src/engine/VoicebotAccount.cpp`
2. `src/engine/SessionManager.cpp`
3. `src/engine/CapacityManager.cpp`

예시 요구:

1. 특정 서비스는 full이어도 busy 말고 302로 돌리고 싶다.
2. Redis 불가 시 fail-open으로 받고 싶다.
3. 로컬 동시호와 분산 슬롯 정책을 다르게 가져가고 싶다.

이 경우는 거의 항상 코드 수정입니다.

### 9.3 AI 프로토콜을 바꾸고 싶다

수정 순서:

1. `protos/voicebot.proto`
2. `cmake --build build`
3. `src/ai/VoicebotAiClient.cpp`
4. 필요 시 `src/engine/VoicebotMediaPort.cpp`
5. Mock 서버 `src/emulator/mock_server.py`

자주 있는 변경:

1. AudioChunk 필드 추가
2. AiResponse 타입 추가
3. STT 텍스트 외 메타데이터 추가
4. DTMF나 control event를 별도 타입으로 분리

### 9.4 녹취 정책을 바꾸고 싶다

주요 파일:

1. `src/engine/VoicebotCall.cpp`
2. `src/api/HttpServer.cpp`
3. `src/utils/AppConfig.h`

현재 동작:

1. `CALL_RECORDING_ENABLE=1`이면 media 연결 후 자동 시작
2. HTTP API로 개별 시작/중지 가능
3. stop 시 녹취만 멈추고 live AI media path는 유지

수정 포인트:

1. 파일명 규칙
2. 저장 디렉토리
3. start/stop 정책
4. 용량/보관 정책

### 9.5 상담원 브리지 또는 AI pause 정책을 바꾸고 싶다

주요 파일:

1. `src/engine/VoicebotCall.cpp`
2. `src/engine/VoicebotMediaPort.cpp`

현재 구조:

1. `bridgeWith()`가 두 통화의 `AudioMedia`를 상호 연결
2. bridge 동안 `media_port_->setAiPaused(true)`
3. unbridge 시 AI 전달 복구

즉, 브리지 문제는 SIP signaling보다 media wiring과 AI pause 상태를 같이 봐야 합니다.

### 9.6 관리자 API를 추가하고 싶다

표준 절차:

1. `src/api/HttpServer.cpp`에 route 추가
2. 입력 JSON 파싱 함수 추가 또는 기존 helper 재사용
3. `X-Admin-Key` 인증 적용
4. 유효성 검증 추가
5. 감사 로그 추가
6. `VoicebotCall` 또는 매니저 계층 메서드 추가
7. `/health`나 `/metrics`에 필요한 관측성 추가
8. 테스트/문서 업데이트

현재 API 종류:

1. health/live/ready/metrics
2. outbound call
3. DTMF
4. transfer
5. record start/stop
6. bridge/unbridge
7. active sessions
8. per-call stats

### 9.7 gRPC 재연결 정책을 바꾸고 싶다

주요 파일:

1. `src/utils/AppConfig.h`
2. `src/ai/VoicebotAiClient.cpp`

현재 동작:

1. write 실패 시 read worker가 reconnect 담당
2. 지수 백오프 적용
3. reconnect 성공 후 stale audio queue flush
4. 영구 실패는 error callback으로 `VoicebotCall`에 전파

수정 시 체크:

1. queue flush 정책
2. permanent vs transient error 분류
3. stream deadline
4. metrics 증분

### 9.8 VAD 감도 또는 전처리를 바꾸고 싶다

주요 파일:

1. `src/ai/SileroVad.cpp`
2. `src/ai/SpeexDsp.cpp`
3. `src/engine/VoicebotMediaPort.cpp`

확인 사항:

1. 입력 샘플 레이트는 16kHz 기준인지
2. frame size가 512 sample 기반인지
3. speech start/end callback이 어떤 조건으로 호출되는지
4. TTS clear 시 VAD reset이 필요한지

### 9.9 production 보안 정책을 바꾸고 싶다

주요 파일:

1. `src/utils/AppConfig.h`
2. `scripts/validate_prod_env.sh`
3. 관련 운영 문서

현재 production 강제 항목:

1. SIP transport TLS
2. gRPC TLS
3. SRTP enable + mandatory
4. 인증서 파일 존재
5. 강한 `ADMIN_API_KEY`
6. `PJSIP_NULL_AUDIO=0`
7. rate/body/header 제한값 검증
8. `SIP_PORT != HTTP_PORT`
9. NAT/TURN 필수값 검증

정책을 바꾸면 반드시 `validate_prod_env.sh`도 함께 맞추세요.

---

## 10. 관리 API 실전 예시

아래 예시는 모두 `X-Admin-Key`가 필요합니다.

```bash
export ADMIN_API_KEY='replace-with-real-key'
```

### 10.1 서비스 목록

```bash
curl -s http://127.0.0.1:8080/api/v1/services \
  -H "X-Admin-Key: ${ADMIN_API_KEY}"
```

### 10.2 활성 세션 목록

```bash
curl -s http://127.0.0.1:8080/api/v1/sessions/active \
  -H "X-Admin-Key: ${ADMIN_API_KEY}"
```

### 10.3 아웃바운드 콜 생성

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/calls \
  -H "Content-Type: application/json" \
  -H "X-Admin-Key: ${ADMIN_API_KEY}" \
  -d '{"target_uri":"sip:1000@127.0.0.1"}'
```

### 10.4 DTMF 전송

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/calls/<session_id>/dtmf \
  -H "Content-Type: application/json" \
  -H "X-Admin-Key: ${ADMIN_API_KEY}" \
  -d '{"digits":"123#","target":"both"}'
```

`target`는 `peer`, `ai`, `both` 중 하나입니다.

### 10.5 통화 전환

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/calls/<session_id>/transfer \
  -H "Content-Type: application/json" \
  -H "X-Admin-Key: ${ADMIN_API_KEY}" \
  -d '{"target_uri":"sip:agent@pbx-main"}'
```

### 10.6 녹취 시작/중지

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/calls/<session_id>/record/start \
  -H "Content-Type: application/json" \
  -H "X-Admin-Key: ${ADMIN_API_KEY}" \
  -d '{"file_path":"recordings/manual.wav"}'

curl -s -X POST http://127.0.0.1:8080/api/v1/calls/<session_id>/record/stop \
  -H "X-Admin-Key: ${ADMIN_API_KEY}"
```

### 10.7 브리지 연결/해제

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/calls/bridge \
  -H "Content-Type: application/json" \
  -H "X-Admin-Key: ${ADMIN_API_KEY}" \
  -d '{"session_id":"<a>","other_session_id":"<b>"}'

curl -s -X POST http://127.0.0.1:8080/api/v1/calls/unbridge \
  -H "Content-Type: application/json" \
  -H "X-Admin-Key: ${ADMIN_API_KEY}" \
  -d '{"session_id":"<a>","other_session_id":"<b>"}'
```

---

## 11. 상태 확인과 관측성

### 11.1 Health API 의미

| 엔드포인트 | 의미 | 실패 시 해석 |
|-----------|------|-------------|
| `/live` | 프로세스가 살아 있는가 | 프로세스 자체 문제 |
| `/ready` | SIP/gRPC 기준으로 트래픽을 받아도 되는가 | 등록/AI 연결 문제 |
| `/health` | 운영자용 요약 JSON | 상세 진단용 |
| `/metrics` | Prometheus 지표 | 대시보드/알람용 |

### 11.2 자주 보는 메트릭

| 메트릭 | 의미 | 이상 징후 |
|--------|------|----------|
| `vbgw_active_calls` | 현재 활성 콜 수 | 급감은 crash/traffic drop, 급증은 overload |
| `vbgw_grpc_active_sessions` | 활성 AI 세션 | `active_calls`와 큰 차이가 나면 누수 의심 |
| `vbgw_grpc_queued_frames` | gRPC 대기 프레임 | 지속 증가 시 backpressure |
| `vbgw_grpc_dropped_frames_total` | 드롭 프레임 누적 | 음질/지연 문제 |
| `vbgw_grpc_reconnect_attempts_total` | gRPC 재연결 횟수 | AI 서버 불안정 |
| `vbgw_grpc_stream_errors_total` | 스트림 오류 누적 | 장애 추적 핵심 |
| `vbgw_vad_speech_events_total` | speech-start edge | VAD 민감도 확인 |
| `vbgw_barge_in_events_total` | barge-in 횟수 | TTS flush 패턴 확인 |
| `vbgw_rtp_rx_lost_total` | RTP 유실 | 네트워크 품질 문제 |
| `vbgw_jbuf_avg_delay_ms_mean` | 평균 jitter buffer 지연 | 지연/품질 tradeoff |
| `vbgw_recording_active_calls` | 녹취 중 통화 수 | 녹취 정책 확인 |

### 11.3 `/health` JSON에서 꼭 볼 필드

1. `status`
2. `profile`
3. `active_calls`
4. `sip.registered`
5. `grpc.healthy`
6. `grpc.active_sessions`
7. `grpc.stream_errors_total`
8. `rtp.mean_rx_jitter_usec`
9. `rtp.mean_jbuf_delay_ms`

---

## 12. 테스트 절차

### 12.1 가장 먼저 돌릴 기본 검증

```bash
cmake --build build
ctest --test-dir build --output-on-failure -E hiredis-test
```

이 조합은 최소한 다음을 검증합니다.

1. 빌드 가능성
2. ring buffer
3. runtime metrics
4. session manager

### 12.2 Redis 검증

```bash
./build/verify_redis
```

의미:

1. Redis 주소와 연결 가능성
2. lease/heartbeat/release 흐름
3. 분산 슬롯 계층의 기본 동작

### 12.3 Webhook 검증

```bash
./build/verify_webhook
```

필요 시 별도 mock 서버를 함께 띄워야 합니다.

### 12.4 SIPp 단일 시나리오 검증

질문 오디오 재생 후 AI 응답 흐름을 보는 스크립트:

```bash
ARTIFACT_DIR=logs/sipp_question_test ./scripts/run_sipp_question_tts_test.sh
```

이 스크립트는 다음을 자동화합니다.

1. mock AI 서버 기동
2. vbgw 기동
3. 질문용 WAV 또는 PCAP 생성
4. RTP replay
5. SIPp call scenario 수행

### 12.5 SIPp 부하 테스트

```bash
ARTIFACT_DIR=logs/load_test_10min \
TARGET_PORT=5060 \
HTTP_PORT=8080 \
AI_PORT=55051 \
TEST_DURATION_SEC=600 \
TOTAL_CALLS=6000 \
./scripts/load_test_10min.sh
```

기본값:

1. `CALL_RATE=10`
2. `CONCURRENCY_LIMIT=200`
3. `HOLD_MS=10000`

동시호 계산 감각:

`동시호 ≈ CPS × 평균 통화 유지시간(초)`

즉, `10 CPS`와 `10초`면 대략 `100 동시호`를 감당해야 합니다.

### 12.6 코드 변경 후 권장 검증 순서

1. `cmake --build build`
2. `ctest --test-dir build --output-on-failure -E hiredis-test`
3. 관련 verify binary
4. single-call SIPp 시나리오
5. 짧은 load test
6. 필요 시 10분 load test

---

## 13. 자주 발생하는 문제와 해석법

### 13.1 `MAX_CONCURRENT_CALLS`를 올렸는데 실제 수용량이 그대로다

원인 후보:

1. PJPROJECT compile-time cap이 더 낮음
2. startup 로그의 `effective` cap이 낮게 잡힘

해결:

1. `scripts/build_local_pjproject.sh`
2. `scripts/configure_with_local_pjproject.sh`
3. startup 로그에서 `compile_time_cap` 확인

### 13.2 `/ready`가 503이다

확인 순서:

1. PBX mode인지 local mode인지
2. SIP 등록 상태
3. gRPC active session이 0인데 unhealthy인지, active session이 있는데 unhealthy인지
4. mock AI 또는 실제 AI 서버가 살아 있는지

### 13.3 통화는 되는데 AI 응답이 없다

확인 순서:

1. `VoicebotCall`에서 AI 세션이 시작됐는지
2. `VoicebotMediaPort`가 실제로 audio frame을 AI로 보내는지
3. `vad_triggers`가 올라가는지
4. mock server 로그에 STT/TTS 이벤트가 있는지
5. gRPC reconnect/error counter가 증가하는지

### 13.4 `486 Busy Here`가 많다

확인 순서:

1. 로컬 `MAX_CONCURRENT_CALLS`
2. 서비스별 `capacity.max_concurrent`
3. Redis lease active count
4. compile-time call slot cap

### 13.5 녹취를 멈추면 AI 오디오도 같이 끊긴다

현재는 수정되어 있어야 정상입니다. 만약 다시 재발한다면 `VoicebotCall::stopRecording()`에서 recorder stop과 media path teardown이 섞여 있지 않은지 확인하세요.

### 13.6 Redis가 죽으면 어떻게 되나

현재 구현은 lease 확보 실패 시 인입호를 받지 못할 수 있습니다. 즉 “Redis 장애 시 fail-open”이 기본 정책이 아닙니다. 운영 요구가 다르면 `CapacityManager` 정책을 의도적으로 바꿔야 합니다.

---

## 14. 새 기능을 넣을 때의 표준 작업 순서

이 순서를 지키면 기능은 추가됐는데 문서와 운영이 따라오지 않는 상황을 줄일 수 있습니다.

1. 요구사항을 문장으로 적는다.
2. 관련 계층을 결정한다.
3. `AppConfig`에 새 설정이 필요한지 판단한다.
4. `VoicebotCall` 또는 하위 계층에서 실제 로직을 구현한다.
5. 필요 시 `HttpServer` API를 노출한다.
6. `RuntimeMetrics`와 `/metrics`를 확장한다.
7. 단위 테스트 또는 verify 테스트를 추가한다.
8. SIPp 또는 mock 기반 시나리오를 추가한다.
9. 운영 문서와 이 문서를 업데이트한다.

### 14.1 새 기능이 어느 계층에 속하는지 고르는 기준

| 요구 | 1차 수정 지점 |
|------|---------------|
| 환경변수/기본값 | `src/utils/AppConfig.h` |
| 인입호 정책 | `src/engine/VoicebotAccount.cpp` |
| 콜 중 제어 | `src/engine/VoicebotCall.cpp` |
| 음성 전처리/VAD | `src/engine/VoicebotMediaPort.cpp`, `src/ai/*` |
| AI 프로토콜 | `protos/voicebot.proto`, `src/ai/VoicebotAiClient.cpp` |
| 운영 API | `src/api/HttpServer.cpp` |
| 라우팅 문법 | `src/engine/RoutingEngine.cpp` |
| 분산 용량 정책 | `src/engine/CapacityManager.cpp` |
| 프로세스 lifecycle | `src/main.cpp`, `SessionManager.cpp` |

---

## 15. 운영과 개발이 만나는 경계

이 프로젝트는 개발과 운영의 경계가 명확합니다.

개발자가 바꾸면 운영 영향이 큰 것:

1. TLS/SRTP 정책
2. Redis lease 정책
3. recording 보관 정책
4. rate limit
5. RTP 포트 범위
6. health/readiness 조건

운영자가 조정하면 개발 검증이 필요한 것:

1. `MAX_CONCURRENT_CALLS`
2. `RTP_PORT_MIN/MAX`
3. `GRPC_MAX_RECONNECT_RETRIES`
4. `TTS_BUFFER_SECS`
5. 서비스별 `capacity.max_concurrent`

즉, 코드 수정 전에 “이 변경이 운영 의미를 바꾸는가”를 먼저 보는 습관이 중요합니다.

---

## 16. 추천 학습 순서

처음 투입된 개발자에게 추천하는 온보딩 순서는 아래입니다.

1. 이 문서 전체를 한 번 읽기
2. `src/main.cpp`와 `AppConfig.h` 읽기
3. `VoicebotAccount -> VoicebotCall -> VoicebotMediaPort -> VoicebotAiClient` 순서로 읽기
4. `config/routing.yaml` 읽기
5. `ctest` 돌리기
6. `run_sipp_question_tts_test.sh` 실행
7. `load_test_10min.sh` 구조 읽기
8. 작은 API 하나 또는 routing 변경 하나를 실제로 수정해 보기

---

## 17. 함께 보면 좋은 문서

1. `README.md`: 프로젝트 개요와 빠른 시작
2. `docs/operator_manual_ko.md`: 운영자 관점 상세 매뉴얼
3. `docs/developer_customization_ko.md`: 커스터마이징 전략 중심 문서
4. `docs/troubleshooting.md`: 증상별 빠른 해결
5. `docs/api_spec.md`: gRPC 인터페이스 명세
6. `docs/architecture.md`: 설계 관점 아키텍처

이 문서를 읽고도 특정 변경 지점이 헷갈리면, 먼저 “요구가 SIP/미디어/AI/API/운영 중 어느 계층인가”를 분류한 뒤 해당 섹션으로 다시 돌아오면 됩니다.
