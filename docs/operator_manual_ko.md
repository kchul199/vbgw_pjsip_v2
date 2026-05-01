# VoiceBot Gateway 상세 운영자 매뉴얼 (한국어)

이 문서는 운영자, SRE, DevOps 엔지니어, 고객사 기술지원 인력이 **이 문서만 보고 VBGW를 배포, 점검, 장애 대응, 용량 관리**할 수 있도록 작성한 운영 기준 문서입니다.

이 문서의 목표는 네 가지입니다.

1. 어떤 환경변수를 어떻게 넣어야 하는지 명확히 한다.
2. 기동, 중지, 점검, 배포, 부하 테스트 절차를 표준화한다.
3. `/live`, `/ready`, `/health`, `/metrics`, Admin API를 운영 절차에 연결한다.
4. 장애가 났을 때 “무엇부터 확인할지”를 증상별로 바로 찾을 수 있게 한다.

---

## 1. 운영자가 먼저 이해해야 하는 서비스 모델

VBGW는 일반 웹 API 서버가 아닙니다. 운영자는 아래 네 개의 외부 의존성을 동시에 관리해야 합니다.

1. SIP/PBX 또는 SBC
2. RTP 미디어 경로
3. AI gRPC 서버
4. Redis

즉, “프로세스가 떠 있다”만으로는 정상 운영이 아닙니다. 아래 네 가지가 모두 정상이여야 합니다.

1. SIP 수신 또는 등록 상태 정상
2. RTP 포트 범위 정상
3. AI gRPC 세션 생성 가능
4. Redis lease 확보 가능

---

## 2. 운영 토폴로지

### 2.1 기본 토폴로지

```text
PBX/SBC
  <-> SIP signaling
VBGW
  <-> RTP media
VBGW
  <-> gRPC bi-directional streaming
AI Engine
  <-> Redis
Capacity / Session Coordination
```

### 2.2 HTTP 관리 평면

운영자가 직접 보는 것은 별도의 HTTP 포트입니다.

1. `/live`
2. `/ready`
3. `/health`
4. `/metrics`
5. `/api/v1/*`

이 포트는 SIP 포트와 절대 같으면 안 됩니다. production 검증에서도 실패 처리됩니다.

### 2.3 로컬 테스트 모드와 운영 모드의 차이

| 항목 | 로컬/테스트 | 운영 |
|------|-------------|------|
| `VBGW_PROFILE` | `dev` | `production` 권장 |
| `GRPC_USE_TLS` | 보통 `0` | `1` 필수 |
| `SIP_TRANSPORT_TLS_ENABLE` | 선택 | `1` 필수 |
| `SRTP_ENABLE` | 선택 | `1` 필수 |
| `SRTP_MANDATORY` | 선택 | `1` 필수 |
| `PJSIP_NULL_AUDIO` | 테스트에서 `1` 가능 | `0` 필수 |
| AI 서버 | mock 가능 | 실제 엔진 |
| Redis | 로컬 단일 인스턴스 가능 | 고가용성 권장 |

---

## 3. 운영 전 필수 체크리스트

### 3.1 네트워크

1. SIP 포트가 열려 있는가
2. RTP UDP 포트 범위가 열려 있는가
3. HTTP Admin 포트가 내부망에서 접근 가능한가
4. AI gRPC 주소로 TCP 연결 가능한가
5. Redis 주소로 TCP 연결 가능한가

### 3.2 인증서/키

production 프로파일에서는 다음 파일이 실제로 존재해야 합니다.

1. `SIP_TLS_CERT_FILE`
2. `SIP_TLS_PRIVKEY_FILE`
3. `SIP_TLS_CA_FILE`
4. `GRPC_TLS_CA_CERT`
5. `GRPC_TLS_CLIENT_CERT`
6. `GRPC_TLS_CLIENT_KEY`

### 3.3 보안 정책

production에서는 다음이 강제됩니다.

1. SIP transport TLS 활성화
2. gRPC TLS 활성화
3. SRTP 활성화
4. SRTP mandatory
5. `ADMIN_API_KEY` 강한 값 사용
6. `PJSIP_NULL_AUDIO=0`
7. body/header/rate limit 값이 허용 범위 내
8. `SIP_PORT != HTTP_PORT`

### 3.4 사전 검증 명령

배포 전에 환경 파일을 검증합니다.

```bash
./scripts/validate_prod_env.sh .env
```

운영 프로파일 강제 검증:

```bash
VALIDATE_PROFILE=production REQUIRE_PRODUCTION_PROFILE=1 \
  ./scripts/validate_prod_env.sh .env
```

---

## 4. 환경변수 운영 기준

모든 설정의 중심은 `src/utils/AppConfig.h`입니다. 운영자는 최소한 아래 그룹을 분류해서 관리해야 합니다.

### 4.1 필수 핵심 그룹

| 변수 | 필수 여부 | 운영 기준 |
|------|-----------|----------|
| `VBGW_PROFILE` | 필수 | 운영은 `production` 권장 |
| `SIP_PORT` | 필수 | SIP 리슨 포트 |
| `HTTP_PORT` | 필수 | 관리 API 포트 |
| `AI_ENGINE_ADDR` | 필수 | `host:port` 형식 |
| `MAX_CONCURRENT_CALLS` | 필수 | 프로세스당 상한 |
| `ADMIN_API_KEY` | 필수 | 16자 이상, 대소문자/숫자/특수문자 포함 |
| `REDIS_ADDR` | 강력 권장 | Redis 연결 주소 |

### 4.2 SIP/PBX 그룹

| 변수 | 의미 | 운영 체크포인트 |
|------|------|----------------|
| `PBX_MAIN_URI` | 메인 registrar/proxy | 실제 PBX reachable 여부 |
| `PBX_MAIN_ID_URI` | 메인 계정 URI | 계정과 URI 일치 |
| `PBX_MAIN_USERNAME` | 메인 사용자명 | 인증 일치 |
| `PBX_MAIN_PASSWORD` | 메인 비밀번호 | Secret으로 관리 |
| `PBX_MAIN_REGISTER_ENABLE` | 메인 등록 | inbound/outbound 정책에 맞춤 |
| `PBX_STANDBY_*` | 스탠바이 계정 | standby 라우팅 계획 필요 |

주의:

1. 현재 failover는 등록 상태 중심입니다.
2. 완전한 SIP OPTIONS 헬스체크로 착각하면 안 됩니다.

### 4.3 SIP 트랜스포트/보안 그룹

| 변수 | 운영 기준 |
|------|-----------|
| `SIP_TRANSPORT_UDP_ENABLE` | 환경에 맞게 사용 |
| `SIP_TRANSPORT_TCP_ENABLE` | 필요한 경우만 |
| `SIP_TRANSPORT_TLS_ENABLE` | production 필수 |
| `SIP_TRANSPORT_PREFERRED` | `udp`, `tcp`, `tls` 중 명시 |
| `SIP_USE_TLS` | TLS 사용 상위 스위치 |
| `SRTP_ENABLE` | production 필수 |
| `SRTP_MANDATORY` | production 필수 |
| `SIP_TLS_CERT_FILE` | 실제 경로 검증 |
| `SIP_TLS_PRIVKEY_FILE` | 실제 경로 검증 |
| `SIP_TLS_CA_FILE` | 실제 경로 검증 |

### 4.4 NAT/STUN/TURN 그룹

| 변수 | 운영 판단 기준 |
|------|---------------|
| `SIP_STUN_SERVER` | NAT traversal이 필요할 때 |
| `SIP_STUN_SIP_ENABLE` | SIP 주소 변환 필요 시 |
| `SIP_STUN_MEDIA_ENABLE` | 미디어 NAT 문제 시 |
| `SIP_NAT_CONTACT_REWRITE_ENABLE` | NAT 뒤에 있으면 보통 유지 |
| `SIP_NAT_VIA_REWRITE_ENABLE` | SIP 경로 문제 해결 시 |
| `SIP_NAT_SDP_REWRITE_ENABLE` | SDP rewrite 필요 시만 |
| `SIP_NAT_SIP_OUTBOUND_ENABLE` | NAT outbound 유지 목적 |
| `SIP_ICE_ENABLE` | WebRTC 또는 복잡한 NAT 환경 |
| `SIP_TURN_ENABLE` | TURN 필요 환경 |
| `SIP_TURN_SERVER` | TURN 주소 |
| `SIP_TURN_USERNAME` | TURN 계정 |
| `SIP_TURN_PASSWORD` | TURN 비밀번호 |

production에서 TURN을 켠다면 username/password 누락 시 기동 전에 차단됩니다.

### 4.5 AI/gRPC 그룹

| 변수 | 운영 기준 |
|------|-----------|
| `AI_ENGINE_ADDR` | 반드시 `host:port` |
| `GRPC_USE_TLS` | production 필수 |
| `GRPC_TLS_CA_CERT` | CA bundle |
| `GRPC_TLS_CLIENT_CERT` | client cert |
| `GRPC_TLS_CLIENT_KEY` | client key |
| `GRPC_STREAM_DEADLINE_SECS` | 장시간 통화 기준 조정 |
| `GRPC_MAX_RECONNECT_RETRIES` | AI 서버 안정성에 맞게 |
| `GRPC_MAX_BACKOFF_MS` | 재연결 민감도 |

운영 해석:

1. `grpc_stream_errors_total`이 느리게 증가하면 AI 서버 품질 이슈일 수 있습니다.
2. `grpc_queued_frames`가 계속 쌓이면 backpressure입니다.

### 4.6 미디어/품질 그룹

| 변수 | 운영 기준 |
|------|-----------|
| `RTP_PORT_MIN`, `RTP_PORT_MAX` | 방화벽과 정확히 일치 |
| `JB_INIT_MS` | 평균 지연 감수 범위 |
| `JB_MIN_PRE_MS`, `JB_MAX_PRE_MS`, `JB_MAX_MS` | jitter와 latency tradeoff |
| `TTS_BUFFER_SECS` | TTS burst 대응 |
| `SPEEX_DENOISE_ENABLE` | 노이즈 환경이면 유지 |
| `SPEEX_AGC_ENABLE` | 입력 볼륨 편차가 크면 유지 |
| `SPEEX_AGC_LEVEL` | 과도한 증폭 여부 확인 |

### 4.7 운영 API/관제 그룹

| 변수 | 운영 기준 |
|------|-----------|
| `HTTP_PORT` | 내부망 전용 권장 |
| `ADMIN_API_KEY` | Secret 관리 |
| `ADMIN_API_RATE_LIMIT_RPS` | 남용 방지 |
| `ADMIN_API_RATE_LIMIT_BURST` | burst 허용치 |
| `ADMIN_API_MAX_BODY_BYTES` | 요청 바디 제한 |
| `ADMIN_API_MAX_HEADER_BYTES` | 헤더 제한 |

### 4.8 녹취/웹훅/Redis 그룹

| 변수 | 운영 기준 |
|------|-----------|
| `CALL_RECORDING_ENABLE` | 개인정보 정책과 함께 결정 |
| `CALL_RECORDING_DIR` | 빠른 디스크 + 백업 정책 확인 |
| `CALL_RECORDING_MAX_DAYS` | 보관정책 |
| `CALL_RECORDING_MAX_MB` | 디스크 상한 |
| `CDR_WEBHOOK_ENABLE` | 외부 적재 필요 시 |
| `CDR_WEBHOOK_URL` | 수신 서버 SLA 확인 |
| `REDIS_ADDR` | 운영 Redis 주소 |

---

## 5. 배포 절차

### 5.1 표준 배포 순서

1. 환경 파일 준비
2. 인증서/비밀키 배포
3. 네트워크 포트 오픈 확인
4. `validate_prod_env.sh` 실행
5. 애플리케이션 기동
6. `/live`, `/ready`, `/health`, `/metrics` 확인
7. 실제 SIP test 또는 SIPp smoke 수행

### 5.2 빌드 방식 선택

운영에서 동시호가 크지 않다면 기본 패키지 PJPROJECT로도 충분할 수 있습니다. 하지만 아래 중 하나면 커스텀 PJPROJECT 빌드를 권장합니다.

1. 4콜 또는 그 근처에서 수용량이 막히는 환경
2. 30콜 이상을 안정적으로 처리해야 하는 환경
3. 10 CPS 이상의 부하 테스트를 정기적으로 수행하는 환경

커스텀 빌드:

```bash
./scripts/build_local_pjproject.sh
BUILD_DIR=build-local ./scripts/configure_with_local_pjproject.sh -DCMAKE_BUILD_TYPE=Release
cmake --build build-local
```

### 5.3 기동 명령 예시

```bash
export VBGW_PROFILE=production
export SIP_PORT=5060
export HTTP_PORT=8080
export AI_ENGINE_ADDR=ai.example.internal:50051
export REDIS_ADDR=tcp://redis.example.internal:6379
export ADMIN_API_KEY='replace-with-strong-secret'
./build/vbgw
```

### 5.4 기동 직후 운영 점검

```bash
curl -s http://127.0.0.1:8080/live
curl -s http://127.0.0.1:8080/ready
curl -s http://127.0.0.1:8080/health
curl -s http://127.0.0.1:8080/metrics | head -n 50
```

체크 포인트:

1. `/live`는 무조건 200이어야 함
2. `/ready`는 SIP/gRPC 상태에 따라 200 또는 503
3. `/health.status`는 `UP` 또는 `DEGRADED`
4. `sip.registered`, `grpc.healthy`, `active_calls`가 기대값인지 확인

---

## 6. 정상 기동과 종료 절차

### 6.1 시작 절차

1. Redis 준비
2. AI 서버 준비
3. VBGW 기동
4. readiness 통과 대기
5. SIP 트래픽 인입

### 6.2 정상 종료 절차

이 애플리케이션은 graceful shutdown 순서를 가집니다.

종료 시 내부 순서:

1. HTTP 서버 중지
2. AI 세션 종료
3. 활성 콜 hangup
4. 계정 probing 중지
5. 계정 shutdown
6. endpoint shutdown

권장 시그널:

1. `SIGINT`
2. `SIGTERM`

피해야 할 것:

1. `kill -9`
2. 진행 중 통화가 많은 상황에서 무계획 프로세스 강제 제거

### 6.3 종료 전 drain 전략

운영 환경에서는 새 트래픽 유입을 먼저 막고, 기존 통화가 빠지도록 기다린 뒤 프로세스를 내리는 것이 가장 안전합니다.

최소 절차:

1. SIP 상위 레이어에서 신규 라우팅 차단
2. `vbgw_active_calls`가 충분히 줄 때까지 관찰
3. 종료 시그널 전달

---

## 7. 관제와 대시보드

### 7.1 상태 API 의미

| API | 의미 | 운영 해석 |
|-----|------|----------|
| `/live` | 프로세스 생존 | 실패 시 즉시 재시작 대상 |
| `/ready` | 트래픽 수용 가능 여부 | 503이면 LB에서 제외 |
| `/health` | 사람이 읽는 요약 상태 | incident triage 시작점 |
| `/metrics` | Prometheus 수집 | 대시보드/알람 기반 |

### 7.2 `/health` 주요 필드

| 필드 | 의미 |
|------|------|
| `status` | `UP` 또는 `DEGRADED` |
| `profile` | 현재 프로파일 |
| `active_calls` | 활성 통화 수 |
| `sip.mode` | `PBX` 또는 `LOCAL` |
| `sip.registered` | 등록 상태 |
| `sip.last_status_code` | 최근 SIP 응답 |
| `grpc.healthy` | gRPC 상태 |
| `grpc.active_sessions` | AI 세션 수 |
| `grpc.queued_frames` | 전송 대기 프레임 |
| `grpc.dropped_frames_total` | 드롭 프레임 누적 |
| `grpc.reconnect_attempts_total` | 재연결 누적 |
| `grpc.stream_errors_total` | 스트림 오류 누적 |
| `rtp.mean_rx_jitter_usec` | 평균 수신 지터 |
| `rtp.mean_rtt_usec` | 평균 왕복 시간 |
| `rtp.mean_jbuf_delay_ms` | 평균 jitter buffer 지연 |

### 7.3 핵심 Prometheus 메트릭

운영자가 가장 자주 보는 메트릭:

1. `vbgw_active_calls`
2. `vbgw_sip_registered`
3. `vbgw_grpc_active_sessions`
4. `vbgw_grpc_queued_frames`
5. `vbgw_grpc_dropped_frames_total`
6. `vbgw_grpc_reconnect_attempts_total`
7. `vbgw_grpc_stream_errors_total`
8. `vbgw_vad_speech_events_total`
9. `vbgw_barge_in_events_total`
10. `vbgw_rtp_rx_packets_total`
11. `vbgw_rtp_rx_lost_total`
12. `vbgw_rtp_rx_jitter_usec_mean`
13. `vbgw_jbuf_avg_delay_ms_mean`
14. `vbgw_recording_active_calls`
15. `vbgw_admin_api_outbound_requests_total`
16. `vbgw_admin_api_outbound_failed_total`

### 7.4 권장 알람 조건

아래는 시작점입니다.

1. `/ready != 200`가 2분 이상 지속
2. `vbgw_sip_registered == 0`이 PBX 모드에서 1분 이상 지속
3. `increase(vbgw_grpc_stream_errors_total[5m]) > 0`
4. `increase(vbgw_grpc_dropped_frames_total[5m]) > 0`
5. `vbgw_active_calls` 급감 또는 급증
6. `increase(vbgw_rtp_rx_lost_total[5m])` 급증
7. `vbgw_grpc_active_sessions`와 `vbgw_active_calls` 간 차이가 비정상적으로 큼

---

## 8. 운영 API 실무 절차

운영자는 읽기 API와 제어 API를 구분해서 사용해야 합니다.

### 8.1 읽기 API

```bash
curl -s http://127.0.0.1:8080/api/v1/services \
  -H "X-Admin-Key: $ADMIN_API_KEY"

curl -s http://127.0.0.1:8080/api/v1/sessions/active \
  -H "X-Admin-Key: $ADMIN_API_KEY"

curl -s http://127.0.0.1:8080/api/v1/calls/<session_id>/stats \
  -H "X-Admin-Key: $ADMIN_API_KEY"
```

용도:

1. 어느 서비스가 차 있는지 확인
2. 활성 세션 목록 확인
3. 특정 통화의 RTP 품질과 상태 확인

### 8.2 제어 API

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/calls \
  -H "Content-Type: application/json" \
  -H "X-Admin-Key: $ADMIN_API_KEY" \
  -d '{"target_uri":"sip:1000@pbx-main"}'
```

추가 제어:

1. DTMF 전송
2. Blind transfer
3. Record start/stop
4. Bridge/unbridge

운영 원칙:

1. API 키는 로그에 남기지 않습니다.
2. 사람 수동 제어는 change 기록과 함께 수행합니다.
3. 반복 제어는 rate limit과 충돌하지 않도록 주의합니다.

---

## 9. 용량 관리와 수용량 해석

운영자가 가장 자주 오해하는 포인트가 “MAX_CONCURRENT_CALLS만 올리면 더 받는다”는 가정입니다. 실제 상한은 세 층으로 나뉩니다.

### 9.1 실제 수용량을 결정하는 세 층

1. PJPROJECT compile-time cap
2. `MAX_CONCURRENT_CALLS` 로컬 프로세스 상한
3. `config/routing.yaml`의 서비스별 `capacity.max_concurrent`

### 9.2 분산 슬롯 구조

Redis에는 두 종류의 key가 사용됩니다.

1. `vbgw:slots:{service_name}` ZSET
2. `vbgw:lease:{slot_id}` STRING

lease TTL:

1. 기본 lease TTL은 30초
2. `VoicebotCall`이 10초 heartbeat로 갱신

### 9.3 운영 시 봐야 하는 현상

1. 서비스별 `active_calls`가 `max_concurrent`에 붙는가
2. `Unable to accept incoming call (too many calls)` 로그가 반복되는가
3. Redis 장애 시 lease 실패가 늘어나는가
4. load test에서 `486 Busy Here`가 서비스 정책대로 나오는가

### 9.4 수용량 문제를 좁히는 순서

1. startup 로그에서 `effective` cap 확인
2. `MAX_CONCURRENT_CALLS` 확인
3. `routing.yaml`의 서비스별 `max_concurrent` 확인
4. `/api/v1/services`의 `active_calls` 확인
5. Redis lease active count 확인

---

## 10. 로그 운영 가이드

### 10.1 로그 위치

기본 로그 디렉토리:

1. `LOG_DIR`, 기본값 `logs`

부하 테스트/시나리오 테스트 스크립트는 별도 아티팩트 디렉토리에 다음을 남깁니다.

1. `vbgw_load.log`
2. `mock_server_load.log`
3. `runtime_monitor.log`
4. `load_test_stats.csv`
5. `vbgw.log`
6. `mock_server.log`
7. `rtp_replay.log`

### 10.2 자주 보는 로그 키워드

| 키워드 | 의미 |
|--------|------|
| `AI Stream Session started` | AI 세션 시작 |
| `Unable to accept incoming call` | 수용량 초과 |
| `invalid_admin_key` | 운영 API 인증 실패 |
| `rate_limited` | outbound 제어 API rate limit |
| `Flushed ... stale audio frames` | gRPC 재연결 후 queue flush |
| `Barge-In` | TTS buffer flush 이벤트 |
| `Stream disconnected` | AI 측 스트림 종료/오류 |
| `call_not_found` | 세션 조회 실패 |

### 10.3 로그 레벨 운영 기준

| 레벨 | 사용 상황 |
|------|-----------|
| `info` | 운영 기본 |
| `debug` | 장애 재현/분석 |
| `warn` | 비정상이나 자동 복구 가능 |
| `error` | 수동 개입 가능성 높음 |

---

## 11. 부하 테스트 운영 절차

### 11.1 단일 시나리오 검증

```bash
ARTIFACT_DIR=logs/sipp_question_test ./scripts/run_sipp_question_tts_test.sh
```

이 테스트는 “실제 음성 질문을 보냈을 때 STT/LLM/TTS 흐름이 이어지는가”를 확인할 때 사용합니다.

### 11.2 10 CPS / 10분 부하 테스트

```bash
ARTIFACT_DIR=logs/load_test_10min \
TARGET_PORT=5060 \
HTTP_PORT=8080 \
AI_PORT=55051 \
TEST_DURATION_SEC=600 \
TOTAL_CALLS=6000 \
./scripts/load_test_10min.sh
```

기본적으로 이 스크립트는 다음을 함께 수행합니다.

1. mock AI 서버 기동
2. vbgw 기동
3. 주기적 `/health`, `/metrics` 수집
4. SIPp 부하 호출
5. 아티팩트 저장

### 11.3 운영 해석 포인트

성공 여부는 “프로세스가 살아남았는가”만 보면 안 됩니다.

함께 봐야 할 것:

1. SIPp 성공/실패 콜 수
2. `vbgw_active_calls` 피크
3. `grpc_active_sessions` 피크
4. `grpc_stream_errors_total`
5. `dropped_frames_total`
6. busy/redirect 비율

---

## 12. 장애 대응 플레이북

### 12.1 증상: 프로세스가 뜨지 않는다

확인 순서:

1. 환경 파일 누락
2. production 보안 정책 위반
3. 인증서 경로 오류
4. `AI_ENGINE_ADDR` 형식 오류
5. 라이브러리 링크 문제

조치:

1. `./scripts/validate_prod_env.sh .env`
2. 시작 로그의 `critical`/`error` 라인 확인
3. ONNX/PJPROJECT/OpenSSL 링크 상태 확인

### 12.2 증상: `/live`는 200인데 `/ready`가 503

원인 후보:

1. PBX 모드에서 SIP 미등록
2. active gRPC session이 있는데 unhealthy

조치:

1. `/health`의 `sip`와 `grpc` 필드 확인
2. PBX 계정/비밀번호/URI 점검
3. AI 서버 reachable 여부 점검

### 12.3 증상: 인입호가 계속 `486 Busy Here`

원인 후보:

1. 서비스별 `capacity.max_concurrent` 초과
2. 로컬 `MAX_CONCURRENT_CALLS` 초과
3. PJPROJECT compile-time cap 낮음
4. Redis lease 확보 실패

조치:

1. `/api/v1/services` 확인
2. startup 로그 확인
3. Redis 연결 확인
4. 필요 시 커스텀 PJPROJECT 빌드 적용

### 12.4 증상: 통화는 연결되는데 AI 응답이 없다

확인 순서:

1. `AI Stream Session started` 로그 확인
2. `grpc_healthy`, `grpc_active_sessions` 확인
3. `vad_speech_events_total` 증가 여부
4. mock/실AI 로그 확인
5. `grpc_stream_errors_total` 증가 여부

### 12.5 증상: 오디오 끊김, 지연, 품질 저하

확인 순서:

1. `vbgw_rtp_rx_lost_total`
2. `vbgw_rtp_rx_jitter_usec_mean`
3. `vbgw_jbuf_avg_delay_ms_mean`
4. `grpc_queued_frames`
5. `grpc_dropped_frames_total`

조치:

1. 네트워크 품질 확인
2. jitter buffer 파라미터 조정
3. AI 서버 응답 지연 확인
4. TTS buffer 길이 점검

### 12.6 증상: Redis 장애

영향:

1. 신규 인입호의 lease 확보 실패
2. 서비스별 active count 조회 실패

조치:

1. Redis 연결성 복구
2. 앱 로그에서 lease 관련 warn/error 확인
3. 서비스 수용정책이 fail-open인지 fail-close인지 요구사항 재확인

### 12.7 증상: 운영 API가 401/403/429를 반환한다

원인:

1. 401: `X-Admin-Key` 누락
2. 403: `X-Admin-Key` 불일치
3. 429: outbound API rate limit 초과

조치:

1. 올바른 키 사용
2. rate limit 값 확인
3. 과도한 자동화 호출 중단

### 12.8 증상: 녹취 디스크 사용량 증가

확인:

1. `CALL_RECORDING_ENABLE`
2. `CALL_RECORDING_DIR`
3. `CALL_RECORDING_MAX_DAYS`
4. `CALL_RECORDING_MAX_MB`
5. `vbgw_recording_active_calls`

조치:

1. 보관 정책 조정
2. 디스크 확장
3. 외부 보관소 적재 정책 수립

---

## 13. 변경 관리 기준

운영 환경에서 아래 항목 변경은 change ticket 또는 동등한 승인 절차를 권장합니다.

1. SIP/TLS/SRTP 정책
2. Redis 주소 또는 lease 정책
3. `MAX_CONCURRENT_CALLS`
4. `RTP_PORT_MIN/MAX`
5. `ADMIN_API_KEY`
6. 녹취 정책
7. `routing.yaml`

### 13.1 변경 후 최소 검증

1. `validate_prod_env.sh`
2. `/live`, `/ready`, `/health`
3. 실제 SIP smoke 또는 SIPp 1콜
4. 필요 시 짧은 load test

---

## 14. 보안 운영 기준

1. `ADMIN_API_KEY`는 Secret 저장소로 관리합니다.
2. 운영 포트는 가능한 내부망에만 노출합니다.
3. production에서는 TLS/SRTP를 끄지 않습니다.
4. gRPC TLS 인증서 만료일을 별도 모니터링합니다.
5. 녹취 파일은 개인정보 처리 정책에 따라 암호화/접근통제합니다.
6. webhook 대상도 TLS와 인증 정책을 검토합니다.

---

## 15. 운영자용 빠른 명령어 모음

### 15.1 상태 확인

```bash
curl -s http://127.0.0.1:8080/live
curl -s http://127.0.0.1:8080/ready
curl -s http://127.0.0.1:8080/health | jq
curl -s http://127.0.0.1:8080/metrics | rg 'vbgw_(active_calls|grpc_active_sessions|grpc_stream_errors_total|rtp_rx_lost_total)'
```

### 15.2 서비스/세션 확인

```bash
curl -s http://127.0.0.1:8080/api/v1/services -H "X-Admin-Key: $ADMIN_API_KEY" | jq
curl -s http://127.0.0.1:8080/api/v1/sessions/active -H "X-Admin-Key: $ADMIN_API_KEY" | jq
```

### 15.3 환경 검증

```bash
./scripts/validate_prod_env.sh .env
```

### 15.4 짧은 smoke test

```bash
ARTIFACT_DIR=logs/smoke_sipp ./scripts/run_sipp_question_tts_test.sh
```

### 15.5 10분 load test

```bash
ARTIFACT_DIR=logs/load_test_10min TEST_DURATION_SEC=600 TOTAL_CALLS=6000 ./scripts/load_test_10min.sh
```

---

## 16. 함께 보면 좋은 문서

1. `README.md`: 프로젝트 개요와 설치
2. `docs/user_manual_ko.md`: 소스 수정 가능한 상세 사용자·개발자 매뉴얼
3. `docs/developer_customization_ko.md`: 커스터마이징 전략
4. `docs/troubleshooting.md`: 증상별 빠른 해결
5. `docs/api_spec.md`: gRPC 명세

운영자는 문제가 생겼을 때 이 문서의 “장애 대응 플레이북”부터 보고, 구조를 깊게 이해해야 할 때 `docs/user_manual_ko.md`를 함께 보면 가장 빠릅니다.
