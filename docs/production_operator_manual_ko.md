# Voicebot Gateway (VBGW) 프로덕션 운영자 메뉴얼

이 문서는 과거의 짧은 요약본을 대체하는 안내 페이지입니다.

실제 운영 절차, 환경변수 기준, 관제 포인트, 부하 테스트, 장애 대응 플레이북은 아래 상세 문서를 기준으로 사용하세요.

1. [상세 운영자 매뉴얼](./operator_manual_ko.md)
2. [상세 사용자·개발자 매뉴얼](./user_manual_ko.md)
3. [트러블슈팅 가이드](./troubleshooting.md)

빠른 이동용 핵심 링크:

1. 배포 전 환경 검증: `./scripts/validate_prod_env.sh`
2. 운영 health/metrics: `/live`, `/ready`, `/health`, `/metrics`
3. 운영 제어 API: `/api/v1/services`, `/api/v1/sessions/active`, `/api/v1/calls/*`

운영자가 가장 자주 확인하는 항목:

1. `vbgw_active_calls`
2. `vbgw_grpc_active_sessions`
3. `vbgw_grpc_stream_errors_total`
4. `vbgw_rtp_rx_lost_total`
5. 서비스별 `active_calls`와 `max_concurrent`

상세 절차가 필요하면 이 문서 대신 반드시 `docs/operator_manual_ko.md`를 먼저 열어 주세요.
