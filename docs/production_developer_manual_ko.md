# Voicebot Gateway (VBGW) 프로덕션 개발자 메뉴얼

이 문서는 과거의 짧은 개발자 요약본을 대체하는 안내 페이지입니다.

실제 소스 수정, 기능 추가, API 확장, 스레드 모델, 테스트 절차는 아래 상세 문서를 기준으로 사용하세요.

1. [상세 사용자·개발자 매뉴얼](./user_manual_ko.md)
2. [개발자 커스터마이징 가이드](./developer_customization_ko.md)
3. [상세 운영자 매뉴얼](./operator_manual_ko.md)

개발자가 가장 먼저 봐야 할 파일:

1. `src/main.cpp`
2. `src/utils/AppConfig.h`
3. `src/engine/VoicebotAccount.cpp`
4. `src/engine/VoicebotCall.cpp`
5. `src/engine/VoicebotMediaPort.cpp`
6. `src/ai/VoicebotAiClient.cpp`
7. `src/api/HttpServer.cpp`

수정 후 최소 검증:

```bash
cmake --build build
ctest --test-dir build --output-on-failure -E hiredis-test
```

SIP/gRPC/E2E까지 확인하려면 다음 스크립트를 사용하세요.

1. `./scripts/run_sipp_question_tts_test.sh`
2. `./scripts/load_test_10min.sh`

상세 작업 절차는 반드시 `docs/user_manual_ko.md`를 기준으로 따라가세요.
