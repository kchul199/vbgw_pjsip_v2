Listed directory src
Viewed main.cpp:1-373
Edited pjsip_gap_analysis.md

요청하신 대로 최근 완료된 **FreeSWITCH 프로젝트(`vbgw_v2`)를 기준으로, 현재 C++ 기반 PJSIP 프로젝트(`vbgw_pjsip`)에 누락된 기능과 차이점들을 상세히 분석**하여 우측 화면에 `pjsip_gap_analysis.md` 문서로 정리했습니다.

정리된 주요 기능 격차(Gap)는 크게 5가지 카테고리로 분류할 수 있습니다:

1. **라우팅 및 오버플로우 관리**: 동적 YAML 라우팅, 실시간 API 핫리로드, 대기열(Queue Hold) 및 에이전트 호전환(Fallback) 기능 부재
2. **다중 노드 클러스터링**: 스케일 아웃을 위한 Redis Pub/Sub 기반 분산 세션 관리 시스템 미적용 (로컬 메모리만 사용 중)
3. **운영 관측성 (Observability)**: 호 종료 후 외부 시스템 전송용 JSON CDR Webhook 및 디테일한 Prometheus SLA 지표 노출 부재
4. **PBX 헬스체크 및 페일오버**: 메인/서브 PBX 간 능동적 상태 모니터링(Active Probing) 및 다중 게이트웨이 로드밸런싱 우회 로직 부재
5. **AI 파이프라인 방어 로직**: 실시간 쿠션 메시지("잠시만 기다려주세요") TTS 동적 렌더링 및 20턴 초과 시 세션 히스토리 캡핑(Capping) 로직 부족

해당 기능들을 PJSIP에 C++로 직접 이식(포팅)하시려면 `hiredis`(Redis용), `Boost.Beast`(HTTP/Webhook용) 등의 C++ 라이브러리 연동과 함께, FreeSWITCH의 Dialplan이 수행하던 302 Redirect, Early Media Playback 등의 복잡한 SIP 콜 플로우를 `pjsua2` 콜백 내에서 수동으로 구현하셔야 합니다. 

이 문서의 내용을 토대로 PJSIP 프로젝트 고도화 구현(포팅) 계획을 수립하시겠습니까?
