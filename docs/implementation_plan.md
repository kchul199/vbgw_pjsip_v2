# VBGW 솔루션 상용 적용을 위한 개선 작업 계획 (업데이트)

## 목표 설명
본 계획은 VoiceBot Gateway(VBGW) 프로젝트를 프로덕션‑그레이드 수준으로 끌어올리기 위해 현재 평가에서 도출된 **관측성, CI/CD, 테스트 커버리지, 확장성, 보안, 문서·코드 일관성** 등 6대 보완 영역을 구체적인 작업 단계와 담당 영역으로 정리하는 것입니다.

## 사용자 검토 필요
> [!IMPORTANT]
> - 각 개선 작업에 대한 우선순위와 일정(스프린트) 설정이 필요합니다. 스프린트 길이(예: 2주)와 담당자를 지정해 주세요.
> - 기존 CI 파이프라인이 존재한다면 현재 사용 중인 CI 도구(Jenkins, GitHub Actions 등)를 알려 주세요.
> - 배포 환경(Kubernetes 클러스터, Helm 차트 버전, Helm values) 정보가 필요합니다.

## 열린 질문
> [!WARNING]
> - **관측성 도구**: 최신 트렌드에 맞는 스택을 추천해 주세요.
> - **보안 설정**: 최신 트렌드에 맞는 암호화 방법을 추천해 주세요.
> - **멀티 인스턴스**: 유연한 확장을 위해 멀티 인스턴스 배포 구성을 원합니다. SIP/SDP/RTP 연동이 가능한가요?
> - **테스트 프레임워크**: 가장 좋은 C++ 테스트 프레임워크를 제안해 주세요.
> - **CI 도구**: GitHub Actions 로 설정해 주세요.

---

## 제안된 변경 사항
### 1. 관측성 및 메트릭 (추천 스택)
- **Prometheus + Grafana + Loki + OpenTelemetry**
- `src/monitoring` 디렉터리에 Exporter 구현 → SIP 세션, RTP 손실, gRPC 지연, VAD 처리량 등 메트릭 제공.
- OpenTelemetry C++ SDK 연동 → 트레이스 ID를 SIP Call‑ID와 연결.
- spdlog 출력 포맷을 JSON 으로 변환해 Loki 로 전송.
- Helm 차트에 `serviceMonitor` 및 `prometheusRule` 템플릿 추가.
- Grafana 대시보드 템플릿 제공.

### 2. CI/CD 자동화 (GitHub Actions)
- **워크플로**: `.github/workflows/build_test_deploy.yml`
  - **빌드**: CMake + Ninja, Docker 이미지 빌드, 캐시 사용.
  - **테스트**: GoogleTest 실행, 코드 커버리지 (Codecov) 업로드.
  - **정적 분석**: clang‑tidy, cppcheck, cpplint.
  - **보안**: Dependabot, Trivy 이미지 스캔.
  - **배포**: Helm 차트 배포 (스테이징 → 프로덕션) 자동화.
  - **소프트폰 자동 테스트**: SIPp 스크립트로 기본 INVITE/ACK 흐름 검증 후 결과를 아티팩트 저장.

### 3. 테스트 커버리지 강화 (추천 프레임워크)
- **GoogleTest (GTest) + GoogleMock (GMock)** 를 사용.
- `tests/cpp` 에 주요 모듈(Engine, AiClient, MediaPort, SessionManager) 별 유닛 테스트 작성.
- PJSIP, gRPC, ONNX Runtime 을 Mock 객체로 대체하여 독립 테스트 가능.
- CI 에 테스트 실행 및 커버리지 리포트 연계.

### 4. 확장성·멀티 인스턴스 설계
- `max_concurrent_calls` 를 `config/session.yaml` 로 외부화.
- **Redis**(또는 **etcd**) 를 이용해 세션 상태 공유 → 다중 파드 간 동기화.
- **Kubernetes Deployment** + **HorizontalPodAutoscaler** 로 스케일‑아웃.
- **Envoy** 를 gRPC 프록시로 배치해 로드밸런싱 및 서비스 디스커버리 구현.
- SIP/SDP/RTP 연동 시 각 파드가 자체 PJSIP UA 를 띄우고, **NodePort** 혹은 **LoadBalancer** 서비스로 RTP 포트 노출.
- 전체 흐름 Diagram (Mermaid) 제공.

### 5. 보안 강화 (추천 스택)
- **SOPS + age** 로 정적 파일(YAML/JSON) 암호화.
- **HashiCorp Vault** 연동 옵션 제공 – 동적 비밀(TLS 인증서, DB 비밀번호) 조회.
- TLS 1.3 강제, 최신 Cipher Suite 지정, OpenSSL 베스트 프랙티스 적용.
- 파일 권한 최소화(실행 파일 600, 설정 파일 600).
- `SECURITY.md` 작성 및 정기 보안 스캔 자동화 (Trivy).

### 6. 문서·코드 일관성 정비
- `scripts/doc_sync.py` 로 한국어·영문 문서를 최신 코드와 매핑.
- `protos/voicebot.proto` 변경 시 CMake `add_custom_command` 로 `docs/api_spec.md` 자동 재생성.
- 아키텍처·데이터 흐름 다이어그램을 **Mermaid** 로 관리하고 CI에 `markdownlint` 적용.

## 검증 계획
1. **관측성**: Prometheus UI 에서 메트릭 정상 수집, Grafana 대시보드 확인.
2. **CI/CD**: Pull Request → GitHub Actions 자동 실행 → Docker 이미지 레지스트리 푸시 및 Helm 차트 배포 성공 여부 검증.
3. **테스트**: `make test` 로 모든 GoogleTest 실행, 커버리지 80 % 이상 달성.
4. **멀티 인스턴스**: 2+ VBGW 인스턴스를 Kubernetes에 배포, Redis 기반 세션 공유 정상 동작 확인.
5. **보안**: Trivy 이미지 스캔, SOPS 복호화 테스트, TLS 핸드쉐이크 검증(`openssl s_client`).
6. **휴먼 검증 (Softphone 테스트)**:
   - **Softphone**: Zoiper 혹은 Linphone 설치 가이드 제공.
   - **테스트 시나리오**: SIP INVITE → AI 인사말 재생 → 사용자 음성 → VAD → STT → NLU → TTS 응답.
   - **모니터링**: VBGW 로그(`spdlog`), Prometheus 메트릭, Grafana 대시보드 실시간 관찰.
   - **검증 체크리스트**: AI 응답 지연 < 200 ms, 음성 품질 손실 없음, 세션 종료 시 RingBuffer 플러시 확인.
7. **배포**: 스테이징 환경 검증 후 `helm upgrade --install` 으로 프로덕션 배포.

---

## 실행 일정 예시 (2주 스프린트 기준)
| 스프린트 | 주요 작업 |
|----------|-----------|
| 1 | GitHub Actions 설정·기초 테스트 프레임워크(GTest) 구축 |
| 2 | Prometheus exporter, OpenTelemetry 연동 및 Grafana 대시보드 구현 |
| 3 | SOPS + Vault 보안 설정, TLS 강화 |
| 4 | Redis 기반 세션 공유·멀티 인스턴스 배포 설계·Envoy 프록시 구성 |
| 5 | 테스트 커버리지 확대·통합 테스트 스크립트 추가 |
| 6 | 문서 자동화 스크립트·Mermaid 다이어그램 업데이트·휴먼 Softphone 검증 가이드 완성 |

**다음 단계**: 위 계획에 대한 피드백을 받아 스프린트 우선순위와 담당자를 확정하고, `task.md` 를 생성해 상세 작업 항목을 트래킹하도록 하겠습니다.
