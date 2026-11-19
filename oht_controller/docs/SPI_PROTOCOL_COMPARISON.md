# 기존 SPI 규약과 제안 규약 v2 비교

비교 대상:

- [기존 SPI_PROTOCOL.md 보관본](SPI_PROTOCOL_V1.md): 변경 전 28바이트 코드의 규약
- [제안_SPI_PROTOCOL.md](제안_SPI_PROTOCOL.md): 24바이트 요청·응답 규약, 버전 2
- [현재 TSspi.h](../app/TSspi.h), [TSspi.c](../app/TSspi.c), [TScontrol.c](../app/TScontrol.c), [main.h](../app/main.h)

이 문서는 v1 당시 작성한 차이·적용 영향 비교 기록이다. **현재는 제안 v2를 코드·시험·[엑셀](TCP_SPI_Protocol.xlsx)에 적용했다.** 최종 동작과 해석 결정은 [현재 SPI 규약](SPI_PROTOCOL.md)을 따른다. 아래 “기존/현재” 표현은 비교 당시 v1을 가리키며, STM32 펌웨어 구현 및 실물 검증은 별도다.

## 1. 핵심 변화

기존은 Jetson이 목표 위치를 지정하고 실제 위치와 비교하는 구조이다. 제안서는 STM32가 센서 기반 PICK/PLACE/RECOVER 작업을 수행하고, Jetson은 명령 수락·작업 번호·완료 상태로 진행을 판단하는 구조이다. 위치값은 표시·진단용으로 바뀐다.

프레임 크기뿐 아니라 헤더, 필드 위치, 체크섬, 명령과 완료 의미가 달라지므로 **두 규약은 wire 호환되지 않는다.** 한쪽만 변경해서 통신할 수 없다.

## 2. 프레임과 필드 비교

| 항목 | 기존 | 제안 v2 | 적용 영향 |
|---|---|---|---|
| 전체 크기 | 요청·응답 공통 28바이트 | 요청·응답 각각 24바이트 | 전송 길이, DMA 길이, 모든 바이트 변환 갱신 |
| 프레임 형식 | 같은 구조체로 요청·응답 표현 | 요청 구조체와 응답 구조체 분리 | 직렬화·역직렬화도 방향별 분리 |
| magic | uint16, `0xA55A`, offset 0 | 요청 `0xA5`, 응답 `0x5A`, 각 uint8 | 방향별 검사 |
| version | 없음 | offset 1, uint8=2 | 미지원 버전 폐기 |
| 메시지 구분 | offset 2, uint16 msgid | 요청 offset 3의 uint8 cmd, 응답 offset 3은 state | 응답 msgid switch를 필드 판정으로 교체 |
| axis | offset 24, uint16 | offset 2, uint8 | X=1/Z=2 의미는 유지, 크기와 위치 변경 |
| seq | offset 4, uint32 | 요청 offset 4의 uint16 seq | 생성·비교·순환 처리 변경 |
| 수락/실행 번호 | 응답 seq 하나 | 응답 ack_seq(offset 4)와 op_seq(offset 10), 각각 uint16 | 요청 접수와 실제 작업을 따로 추적 |
| len | offset 8, uint16=16 | 없음, 전체 24바이트 고정 | len 검사 제거, 고정 길이 적용 |
| 명령 인자 | position에 목표 위치 | arg(offset 6), target(offset 8), option(offset 12) | 현재 작업 명령은 target=0, option=0 |
| 수락 정보 | ACK msgid 또는 flags bit0 | receipt(offset 6)=0이며 ack_seq 일치 | 수락·거부를 명시적으로 처리 |
| state | offset 10, uint16 | 응답 offset 3, uint8 | 숫자 순서는 0~4 유지 |
| 위치 | offset 12, int32, 단위 미정 | 응답 offset 12, int32, 0~1000‰ | 크기·응답 위치는 같지만 단위·의미 변경 |
| 위치 출처 | 없음 | pos_src(offset 16) | 모름/추정/끝 센서/실측을 구분 |
| heartbeat | offset 16, uint32 | 응답 offset 18, uint16 | 폭·오프셋 변경, 16비트 wrap 허용 |
| fault | offset 20, uint16, 상세값 미정 | 응답 offset 7, uint8, 0~10 정의 | 오류 해석 및 캐시 갱신 |
| 센서/상대축 | 없음 | sensors, relay, remote 추가 | 센서·상대축 인터록 상태 전달 |
| 예약 필드 | 별도 예약 공간 없음 | 요청 8바이트 및 option, 응답 3바이트 | 송신 0 채움·수신 0 검사 |
| checksum | offset 26, 앞 26바이트 합산 & 0xFFFF | offset 22, 앞 22바이트 CRC-16/CCITT-FALSE | 계산 알고리즘과 검증 벡터 교체 |

제안 CRC 파라미터: polynomial=0x1021, init=0xFFFF, refin/refout=false, xorout=0x0000. 제안서의 표준 시험 문자열 `123456789` 결과는 0x29B1이다. 기존 TCP의 합산 체크섬을 바꾸라는 제안은 아니다.

## 3. 명령 체계 비교

| 목적 | 기존 | 제안 v2 |
|---|---|---|
| 응답 클록 | NOP=0x0000 | NOP=0, 상태 조회도 겸함 |
| 이동 | MOVE=0x0010, position=목표 | RECOVER=3, X_MOVE=16, PICK=36, PLACE=37 |
| 상태 조회 | GET_STATUS=0x0012 | NOP로 통합 |
| 수락 응답 | ACK=0x0011 | ack_seq 일치 + receipt=0 |
| 상태 응답 | STATUS=0x0013 | 공통 응답의 state/센서/위치 필드 |
| 완료 응답 | COMPLETE=0x0014 | op_seq 일치 + state=COMPLETE 등 조건 |
| 오류 응답 | ERROR=0xFFFF | 공통 응답의 fault 및 receipt |
| 제어 허용/고장 해제 | 없음 | ARM=1 |
| 즉시 정지 | 없음 | STOP=2, seq·MOVING 검사 우회, 우선 처리 |
| 초기 위치 복구 | 별도 명령 없음 | Z RECOVER → X RECOVER |
| 위치 명령 확장 | 기존 MOVE 자체가 위치 명령 | X_MOVE_POS=17, Z_MOVE_POS=33을 향후용으로 예약 |

X_MOVE의 arg는 정거장 번호로 현재 A=0, B=1이다. PICK은 하강·집게 열기·닫기/접촉 확인·상승을, PLACE는 파지 확인·하강·열기/해제 확인·상승을 STM32가 수행한다. 예약된 위치 명령은 현재 활성 명령과 구분해야 한다.

## 4. 동작과 복구 규칙 비교

| 항목 | 기존 | 제안 v2 |
|---|---|---|
| 기본 교환 | 요청 → 최소 1 ms 대기 → NOP 응답 읽기 | 같은 방식 유지 |
| 응답 반영 지연 | 기본 1 ms 준비 시간을 전제로 교환 | 첫 NOP에 아직 미반영이면 수락 제한시간 500 ms 안에서 NOP 반복 |
| 작업 재전송 | MOVE 자동 재전송 없음 | 작업 명령 자동 재전송 없음 유지, STOP 반복 전송은 허용 |
| 실제 완료 | 대기 MOVE, seq/축 일치, 수락, COMPLETE, 목표 위치 일치, 제한시간 이내 | 대기 작업, 축 일치, ack_seq 일치/receipt=0, op_seq 일치, COMPLETE, fault=0, 제한시간 이내 |
| 완료 위치 비교 | 수행 | 수행하지 않음. STM32의 센서 완료 판정을 신뢰 |
| 완료 상태 유지 | 다음 MOVE 수락까지 | 다음 작업 명령 수락까지 유지 |
| seq 한계 | uint32 최대 도달 시 오류 | uint16 65535 다음 1, 차이 1~32767은 새 요청 |
| 중복 명령 | 같은 seq/목표 재구동 금지 | 같은 seq/cmd/arg/target 재구동 금지 |
| 호스트 재시작 | 세션 동기화 미정, 단독 재시작 지원 안 함 | NOP로 ack_seq 확인 → 다음 seq 설정 → STOP → ARM → RECOVER |
| STM32 부팅 판별 | boot/session 표시 미정 | stage=15 BOOT |
| 오류 후 재가동 | 프로세스 재시작까지 MOVE 금지 유지 | STM32는 최초 fault 유지, ARM 성공으로 해제, RECOVER 후 재개 |
| 실제 정지 | STOP/ABORT 없음을 명시, 로컬 정지 정책 필요 | FAULT 시 Jetson STOP, STM32 정지 확인 조건 정의 |
| 상대축 인터록 | 구체적인 전달 규칙 없음 | 최신 relay 비트를 상대축 remote에 전달 |
| 상대축 데이터 신선도 | 별도 규칙 없음 | 유효 응답이 300 ms 초과하면 remote=0 |

상대축 전달 값은 X→Z에서 `relay & 0x06`(X 정지/유효 정거장), Z→X에서 `relay & 0x01`(Z 상단)이다. 이동 중 NOP에도 remote를 갱신해야 한다. 이는 Heartbeat 변화 시각과 다른 **마지막 유효 응답 시각**으로 신선도를 판단하는 기능이다.

## 5. 제한시간 및 MCU 동작 구체화

| 항목 | 기존 | 제안 v2 |
|---|---|---|
| 상태 조회 / 태스크 대기 | 각 100 ms / 10 ms | 유지, 조회 명령만 NOP로 변경 |
| 유효 응답 / 수락 대기 | 각 500 ms | 유지, 검증·수락 조건 변경 |
| Heartbeat 변화 제한 | 1000 ms | 유지 |
| MCU counter 발생 | 독립 주기 증가, 주기는 합의 필요 | STM32 메인 루프에서 100 ms마다 증가 |
| counter 순환 | uint32 | uint16 |
| 이동 제한 | 일반 MOVE 30 s | X_MOVE/RECOVER 30 s, PICK/PLACE 45 s |
| 장치 재열기 | 1초, 감시만 재개 | 유지 |
| MCU 이동 오류 | 구체적인 수치 미정 | 이동 15 s 초과 TIMEOUT |
| 출발 센서 | 구체적인 조건 미정 | 출발 1.5 s 후 미해제이면 SENSOR |
| MCU 링크 단절 | 로컬 정책 합의 필요 | 유효 요청 500 ms 단절 시 정지; ARM 중 LINK 오류 |

NOP가 계속 도착하면 링크 단절 조건은 발생하지 않는다. 따라서 중앙 FSM이 고장이어도 감시 NOP만 계속 보내는 것으로 정지를 대신할 수 없으며 STOP을 명시적으로 처리해야 한다.

STM32를 F411RE로 특정하고, 제안서의 DMA 설명에서는 해당 MCU에 D-cache가 없다는 전제로 캐시 관리 관련 일반 지침을 줄였다. MISO 공유 시 비선택 MCU의 high-Z 확인도 추가했다.

## 6. 유지되는 부분

- Jetson master / STM32 slave, spidev + ioctl 사용
- 핀맵, X/Z 장치 경로, 축 번호 X=1/Z=2
- Mode 0, 기본 1 MHz, 8 bit/word, MSB first, big endian
- CS를 프레임 사이에 해제하는 요청/NOP 2단계 교환
- state 숫자 순서 UNKNOWN=0, IDLE=1, MOVING=2, COMPLETE=3, FAULT=4
- 태스크와 큐를 통한 명령·이벤트 전달
- 작업 명령의 자동 재실행 금지, 완료 중복 통보 억제
- 두 번째 이후 counter 변화로 Heartbeat 정상 판정
- POSIX/Linux UAPI 사용과 MISRA 편차 관리 필요성

## 7. 코드 적용 범위 — TSspi만 변경해서는 부족함

| 대상 | 필요한 변경 |
|---|---|
| [TSspi.h](../app/TSspi.h) | 24바이트 요청/응답 타입, cmd/receipt/fault/pos_src/stage, uint16 seq/heartbeat, 명령별 제한시간 정의 |
| [TSspi.c](../app/TSspi.c) | 방향별 인코딩/디코딩, CRC-16, NOP 조회, 수락·작업 완료 추적, 응답 지연 허용, remote 갱신, STOP 우선 처리, 재동기화 |
| [main.h](../app/main.h) | 현재 MOVE/GET_STATUS 중심 명령 인터페이스에 ARM/STOP/RECOVER/X_MOVE/PICK/PLACE와 arg 등 추가. 상태 전달 자료형 확장 검토 |
| [TScontrol.c](../app/TScontrol.c) | PICK/PLACE 작업 단위로 시퀀스 수정, 목표 위치 일치 검사 제거·대체, 오류 시 STOP, ARM/RECOVER 및 고장 복귀 경로 설계 |
| STM32 펌웨어 | 프레임/CRC/DMA, 센서 기반 작업 FSM, 인터록, ARM·STOP·고장 유지·링크 감시, 중복 실행 방지 |
| [SPI 통합 시험](../test/test_tsspi.c) | 24바이트 모델, CRC 벡터, 지연 수락, 거부, STOP, wraparound, 재시작, remote 만료, PICK/PLACE 완료 시험 |
| 문서·시퀀스·[엑셀](TCP_SPI_Protocol.xlsx) | 새 프레임·명령·제어 순서로 SPI 관련 부분 갱신. TCP 프레임은 별도 변경 요청이 없다면 유지 |

특히 현재 Z축은 픽업 이동, 인계 이동, 복귀 이동을 중앙 FSM이 나누어 요청한다. 제안 PICK/PLACE는 **STM32 안에서 상승까지 수행**하므로 기존 Z_RETURN을 그대로 보내면 중복 동작이나 순서 불일치가 생길 수 있다. E84 BUSY/COMPLETE를 작업 내부 어느 시점에 맞출지도 합의해야 한다.

위치 설정 및 TSe84 구현을 제안서 범위 밖으로 두더라도, 현재 `TScontrol`의 숫자 목표 위치 검사와 위치 설정 기반 운전 조건을 새 작업 명령에 맞게 연결하는 일은 별도로 필요하다.

## 8. 적용 전에 추가 확인할 문서 항목

아래는 최초 비교 때 확인한 항목이다. 적용 결과: 예약 비0 거부, receipt 거부 시 즉시 고장, STOP은 별도 우선 큐·응답 확인, link bit7 꺼짐/300ms 만료 시 remote=0으로 구현했다. MCU 15s 적용 구간·향후 확장 호환성은 여전히 합의가 필요하다.

1. **예약 필드와 하위 호환성:** 예약 필드가 비0이면 폐기하도록 정했지만 확장 시 같은 version을 유지할 수 있다고도 되어 있다. 비0 예약 필드를 거부하는 구버전과의 호환 방법을 정해야 한다. 기능 협상, 무시 가능한 필드 정책 또는 version 변경 조건을 명확히 할 필요가 있다.
2. **MCU 15 s와 작업 45 s:** MCU의 15 s 제한이 각 이동 구간에 적용되는지 PICK/PLACE 전체에 적용되는지 명확히 해야 한다. 전체 작업 15 s 제한이라면 Jetson의 45 s 대기와 의도가 달라진다.
3. **수락 거부 후 완료 이력:** 새 요청이 BUSY/COMMAND/SEQUENCE로 거부되면 ack_seq/receipt와 이전 op_seq/완료 상태가 함께 남을 수 있다. Jetson이 이전 작업 완료와 현재 거부 응답을 어떻게 구분할지 시험 벡터로 정할 필요가 있다.
4. **STOP의 성공·실패 경로:** 일반 큐 포화, 통신 오류, 한 축 응답 정지 상황에도 STOP 우선 처리와 정지 확인 실패 시 처리를 설계해야 한다. STOP이 실제 전달되지 않으면 정지 완료로 간주할 수 없다.
5. **유효 요청/응답 정의:** remote 300 ms 만료와 링크 500 ms 감시에서 CRC 정상 프레임, 유효 cmd, receipt 거부, NOP를 각각 어떻게 인정하는지 일관되게 정의해야 한다. relay의 link bit를 Jetson이 전달 전에 확인할지도 명시하면 좋다.
6. **정거장 확장과 위치 범위:** 향후 정거장 추가 시 A–끝 구간의 기준 변경 여부, 위치 명령에서 끝 센서/실측 조건을 어떻게 적용할지 확정해야 한다.
7. **누락된 8장:** 제안서의 체크섬 설명은 참조 C 코드·시험 벡터를 8장으로 안내하지만 현재 문서에는 8장이 없다. MCU 측 제공 코드와 요청·응답 전체 바이트 시험 벡터를 추가해야 한다.

## 9. 비교 결론

제안 v2는 기존 규약의 크기 축소에 더해 센서 기반 작업 제어, 실제 정지·재가동, 축 간 인터록 전달을 추가한 설계 변경이다. 기존 드라이버 접근 방식과 큐 구조는 유지할 수 있지만 통신 내용과 중앙 FSM 연결은 함께 변경해야 한다. 비교 기록 작성 후 v2를 코드·시험·엑셀에 적용했다. 제안 원문은 보존하고 기존 규약은 별도 v1 문서로 보관한다.
