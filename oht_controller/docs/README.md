# oht_controller 문서

프로젝트 문서는 이 폴더에서 관리한다. 아래 실행 명령의 기준 디렉터리는 `docs/`의 상위인 `oht_controller/`이다.

- [TCP·SPI 통신 규약 엑셀](TCP_SPI_Protocol.xlsx): 프레임 구조, 메시지 ID, 코드값, 통신 설정과 HEX 예제
- [시퀀스 다이어그램](SEQUENCE_DIAGRAMS.md): 실제 함수명을 표시한 TCP·중앙 FSM·SPI 연동 Mermaid 코드
- [코드 읽는 순서](CODE_GUIDE.md): 중앙 FSM과 SPI의 주요 함수 및 동작 흐름
- [SPI 기존·제안 규약 비교](SPI_PROTOCOL_COMPARISON.md): 28바이트 기존 규약과 24바이트 v2의 차이 및 적용 범위
- [SPI v2 구현 규약](SPI_PROTOCOL.md): 24바이트 프레임, CRC-16, STOP·ARM·RECOVER, 시험과 구현 범위
- [TSspi 트러블슈팅](SPI_TROUBLESHOOTING.md)에서 양축 `00` 응답, Jetson SPI1 설정, 배선·파형, 고장 복구 절차를 확인할 수 있다.
- [제안 v2 원문](제안_SPI_PROTOCOL.md): STM32 센서·DMA·작업 FSM 구현 사항
- [과거 SPI v1 규약](SPI_PROTOCOL_V1.md): 변경 전 28바이트 기록
- [TCP LinkTest 테스터](TCP_LINK_TEST.md): Python 서버 실행과 요청·응답 형식
- [MISRA 적용 기록](MISRA.md): 규칙 적용 및 플랫폼 의존성 기록

구현 코드는 [app/](../app/), 시험 코드는 [test/](../test/)에 있다.
