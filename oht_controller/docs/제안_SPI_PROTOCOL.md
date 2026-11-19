# Jetson–STM32 SPI 통신 규약 및 STM32 펌웨어 구현 사항

이 문서는 TSspi.h / TSspi.c와 STM32 펌웨어가 함께 구현할 24바이트 규약(버전 2)이다.

- 축 번호, 바이트 순서, state 코드, NOP 송수신 방식, 조회 주기는 기존 TSspi 구현을 그대로 따른다. STM32 측이 이 값들에 맞춘다.
- TSspi가 바꿔야 하는 항목은 안전과 장치 특성 때문에 바꿀 수 없는 것만 남겼다(10장 요약).
- 프레임에는 위치 필드와 예약 공간을 둔다. 엔코더 추가, 정거장 추가, 위치 명령 추가가 프레임 변경 없이 가능하도록 하기 위해서다(11장).
- 보정값·제한시간·SPI 속도는 실제 장비에서 합의하고 검증한다.

## 1. 연결과 기본 설정

| 항목 | 현재 구현 |
| --- | --- |
| Jetson 역할 | SPI master |
| STM32 역할 | SPI slave, X/Z 각각 별도 MCU(STM32F411RE) |
| 공유 배선 | 물리 핀 19 MOSI / 21 MISO / 23 SCLK |
| X축 | 물리 핀 24, CS0, STM32②, /dev/spidev0.0, axis=1 |
| Z축 | 물리 핀 26, CS1, STM32①, /dev/spidev0.1, axis=2 |
| 기본 모드 | Mode 0: CPOL=0, CPHA=0 |
| 데이터 | MSB first, 8 bit/word, 다중 바이트 정수는 big endian |
| 초기 속도 | 1 MHz (`TSspi_SPEED_HZ`). 실물 파형으로 확인. 24바이트 전송 시간은 약 0.2 ms |
| CS | active low. 프레임 24바이트 동안 유지, 프레임 사이 해제 |
| 응답 준비 대기 | 요청 CS 해제 후 최소 1 ms (`TSspi_RESPONSE_DELAY_US`) |

장치 경로는 제공된 핀맵 기준이다. 실제 Jetson의 pinmux/device tree 설정과 /dev/spidev* 번호를 확인해야 한다. 이 프로그램은 pinmux, device tree, 장치 권한을 변경하지 않는다. MISO를 두 STM32가 공유하므로 CS 비활성 시 MISO가 high-Z가 되는지 실물로 확인한다.

## 2. 고정 길이 프레임

모든 요청·응답은 24바이트다. C 구조체 전체를 memcpy하거나 sizeof(struct)로 전송하지 않고, 아래 오프셋에 맞춰 명시적으로 인코딩/디코딩한다. 예약 필드는 송신 시 0으로 채우고, 수신 시 0인지 검사한다.

### 요청 (Jetson → STM32)

| 바이트 오프셋 | 크기 | 필드 | 설명 |
| --- | --- | --- | --- |
| 0 | 1 | magic | `0xA5` |
| 1 | 1 | version | `2` |
| 2 | 1 | axis | X=1, Z=2. 실제 선택된 CS와 일치해야 함 |
| 3 | 1 | cmd | 아래 명령 표 |
| 4 | 2 | seq | uint16. NOP는 0 |
| 6 | 1 | arg | 명령 인자. `X_MOVE`는 정거장 번호(0=A, 1=B, 이후 확장) |
| 7 | 1 | remote | 상대 축 응답의 relay 비트를 그대로 넣음(6장) |
| 8 | 4 | target | int32. 위치 명령용 목표 위치(11장). 현재 명령에서는 0 |
| 12 | 2 | option | uint16. 속도 등 향후 옵션용으로 예약. 현재 0 |
| 14 | 8 | reserved | 모두 0 |
| 22 | 2 | crc | 바이트 0~21의 CRC-16 |

### 응답 (STM32 → Jetson)

| 바이트 오프셋 | 크기 | 필드 | 설명 |
| --- | --- | --- | --- |
| 0 | 1 | magic | `0x5A` |
| 1 | 1 | version | `2` |
| 2 | 1 | axis | X=1, Z=2 |
| 3 | 1 | state | 아래 state 코드 |
| 4 | 2 | ack_seq | 마지막으로 받은 유효한 요청의 seq(NOP 제외) |
| 6 | 1 | receipt | 그 요청의 접수 결과. 0=수락, 그 외=거부 사유(fault 코드 값) |
| 7 | 1 | fault | 0: 정상, 그 외: STM32 오류 코드 |
| 8 | 1 | sensors | bit0=A/상단, bit1=B/하단, bit2=집게 접촉(Z만). 선택 사용 |
| 9 | 1 | relay | bit0~2=상대 축에 전달할 relay 비트, bit3~6=stage(선택), bit7=링크 유효 |
| 10 | 2 | op_seq | 실행 중이거나 마지막으로 실행한 작업 명령의 seq |
| 12 | 4 | position | int32. 현재 위치(아래 위치 필드). pos_src=0이면 무시 |
| 16 | 1 | pos_src | 위치 출처: 0=모름, 1=추정, 2=끝 센서 확인, 3=실측(엔코더, 향후) |
| 17 | 1 | reserved | 0 |
| 18 | 2 | heartbeat | uint16. STM32 메인 루프에서 주기적으로 증가 |
| 20 | 2 | reserved | 0. 향후 진단용 |
| 22 | 2 | crc | 바이트 0~21의 CRC-16 |

### 위치 필드

| 항목 | 내용 |
| --- | --- |
| 단위 | 이동 구간의 1/1000(‰). 0=A/상단, 1000=B/하단 |
| 범위 | 0~1000. 정거장이 늘어도 A–끝 구간 기준 비율로 표시 |
| pos_src=1 (추정) | X는 출력 스텝 수, Z는 구동 시간으로 계산. 1~999로 제한 |
| pos_src=2 (끝 센서 확인) | 끝 센서 감지 시 0 또는 1000 |
| pos_src=3 (실측) | 엔코더를 달았을 때 사용. 같은 단위로 보고 |
| pos_src=0 (모름) | 부팅 후 끝 센서를 한 번도 감지하지 못함, 또는 센서 모순 |
| 용도 | 현재 규약에서는 표시·진단 전용. 완료 판정·인터락에 쓰지 않음 |

단위를 mm가 아닌 비율로 정한 이유는 Z 리니어 액추에이터의 실제 이동 거리를 아직 측정하지 않았기 때문이다. 엔코더를 달아도 같은 단위를 쓰면 Jetson 표시 코드는 바뀌지 않는다. mm 단위가 필요해지면 version을 올려 정의한다.

### 체크섬

- CRC-16/CCITT-FALSE를 쓴다. polynomial `0x1021`, init `0xFFFF`, refin/refout=false, xorout=`0x0000`이며, 계산 범위는 바이트 0~21이다.
- 시험 문자열 `123456789`의 결과는 `0x29B1`이다.
- 바이트 합산 체크섬은 바이트 순서 교환 같은 오류를 검출하지 못하므로 쓰지 않는다.
- 참조 C 코드와 시험 벡터는 STM32 측이 제공한다(8장).
- magic·version·axis·reserved·crc 검사에 실패한 프레임은 폐기한다.

### 명령

| cmd | 이름 | 대상 | 동작 |
| --- | --- | --- | --- |
| 0 | `NOP` | X/Z | 응답을 읽기 위한 클록 겸 상태 조회. seq=0. 명령 이력에 반영하지 않음 |
| 1 | `ARM` | X/Z | 제어 허용, 고장 해제 |
| 2 | `STOP` | X/Z | 즉시 정지, ARM 해제. seq·MOVING 검사를 우회 |
| 3 | `RECOVER` | X/Z | 복구(HOME 겸용). Z는 상단으로, X는 A로 이동 |
| 16 | `X_MOVE` | X | arg=정거장 번호. 현재 0=A, 1=B |
| 36 | `PICK` | Z | 하강 → 열기 → 닫기·접촉 확인 → 상승 |
| 37 | `PLACE` | Z | 파지 확인 → 하강 → 열기·해제 확인 → 상승 |

- 기존 MOVE(position)는 위 작업 명령 4개로 대체한다. 이유는 두 가지다. 현재 장치에는 엔코더가 없어 수치 위치를 알 수 없다. 또 위치 명령으로는 Z 하단에서 집을지 놓을지 구분할 수 없다. 위치 명령은 엔코더를 단 뒤 예약 번호로 추가한다(11장).
- GET_STATUS는 NOP와 같다. ACK·STATUS·COMPLETE·ERROR 메시지는 따로 두지 않고, 응답 필드로 판정한다(4장).
- RECOVER는 반드시 Z → X 순서로 보낸다. Z가 상단에 있지 않으면 X의 RECOVER는 거부된다. Z의 RECOVER는 집게를 열지 않는다.
- 정의되지 않은 cmd, 범위를 벗어난 arg, 0이 아닌 target은 receipt=COMMAND로 거부한다.

### state 코드

| 값 | 상태 |
| --- | --- |
| 0 | UNKNOWN (정지·미ARM·부팅 직후) |
| 1 | IDLE |
| 2 | MOVING |
| 3 | COMPLETE |
| 4 | FAULT |

### 오류 코드 (fault, receipt 공용)

| 값 | 이름 | 값 | 이름 |
| --- | --- | --- | --- |
| 0 | OK | 6 | GRIP |
| 1 | STOP | 7 | BUSY |
| 2 | LINK | 8 | COMMAND |
| 3 | SENSOR | 9 | SEQUENCE |
| 4 | TIMEOUT | 10 | TRANSPORT |
| 5 | INTERLOCK | | |

BUSY·COMMAND·SEQUENCE는 receipt(접수 거부)에만 나온다. 기존처럼 fault≠0을 `main_FAULT_SPI_COMMUNICATION` 하나로 전달해도 동작에는 문제가 없다. 상세 코드를 GUI까지 전달하는 것은 후속 확장으로 둔다.

## 3. SPI는 요청 전송과 응답 수신을 두 번으로 나눈다

기존 TSspi의 순서를 그대로 쓴다.

1. STM32는 RX/TX DMA 버퍼를 미리 준비한다.
2. Jetson이 CS를 내리고 요청 24바이트를 전송한 뒤 CS를 해제한다. 이때 MISO로 읽힌 이전 응답은 버린다.
3. STM32는 요청을 검증·해석하고 다음 응답을 준비한다.
4. Jetson은 최소 1 ms 기다린다.
5. Jetson이 같은 CS로 NOP 24바이트를 전송하며 응답을 읽는다.
6. STM32는 NOP를 새 명령으로 취급하지 않는다. ack_seq와 명령 이력도 바꾸지 않는다.

**추가 규칙:** 1 ms 뒤 첫 NOP 응답에는 요청이 아직 반영되지 않았을 수 있다. 이 경우 오류로 보지 않고 수락 제한시간(500 ms) 안에서 NOP를 반복한다. ack_seq가 요청 seq와 같아지면 그 응답으로 접수 결과를 판정한다.

- 각 CS 구간은 정확히 한 프레임이다. 짧거나 긴 전송, CS 중도 해제, SPI overrun이 생기면 STM32는 해당 수신을 버리고 TRANSPORT 고장으로 정지한다. 프레임 경계는 다음 CS에서 복구한다.
- 요청의 결과를 확인하기 전에 통신이 손상되면 명령 실행 여부를 알 수 없다. 이때는 작업 명령을 재전송하지 않고 오류로 전환한다.

### DMA 구현 (STM32 측)

- CS active 전에 TX/RX DMA를 준비한다. 전송 길이는 24바이트다.
- DMA가 쓰는 버퍼와 메인 루프가 쓰는 버퍼를 분리한다.
- SPI 인터럽트 안에서 모터 완료를 기다리지 않는다.
- STM32F411에는 D-cache가 없다.

## 4. 명령 처리와 실제 동작 완료

STM32 측 처리 흐름은 다음과 같다.

1. magic·version·axis·reserved·crc를 검증한다.
2. STOP이면 즉시 정지한다.
3. 그 밖의 명령은 seq, MOVING 여부, ARM·고장 상태, cmd·arg·target 유효성을 확인한다. 이어서 ack_seq와 receipt를 갱신한다.
4. 작업 명령을 수락하면 op_seq를 기록하고 state를 MOVING으로 바꾼다.
5. 메인 루프가 비동기로 동작을 수행한다. 동작 중에는 remote와 로컬 센서로 인터락을 판단한다.
6. 목표 센서로 실제 완료를 확인한 경우에만 state를 COMPLETE로 바꾼다. 집게는 접촉·해제 스위치로 확인한다. 이후 NOP 응답에도 이 완료 상태를 유지한다.
7. 새 작업 명령을 수락하면 이전 완료 상태를 해제한다.

### 인터락과 고장 판정 (STM32)

| 조건 | 처리 |
| --- | --- |
| `X_MOVE`·X `RECOVER` | remote의 Z 상단 비트 필요. 이동 중 사라지면 INTERLOCK |
| Z `RECOVER` | remote의 X 정지 비트 필요 |
| `PICK`·`PLACE` | remote의 X 정지 + X 유효 정거장 비트 필요 |
| 끝 센서 동시 감지 | SENSOR |
| 이동 15 s 초과 | TIMEOUT |
| 출발 1.5 s 후에도 출발 센서 미해제 | SENSOR |
| 집게 닫기·해제 실패, 파지 상실 | GRIP |
| ARM 중 링크 500 ms 두절 | LINK |

### Jetson의 완료 판정

Jetson은 다음 조건을 모두 만족할 때만 중앙 FSM으로 COMPLETE 이벤트를 전달한다.

- 현재 대기 중인 작업 명령이 있음
- 응답 axis와 실제 CS가 일치함
- ack_seq = 대기 seq, receipt = 0 (수락 확인)
- op_seq = 대기 seq, state = COMPLETE
- fault = 0
- 수락·이동·응답·Heartbeat 제한시간을 넘지 않음

위치 비교는 하지 않는다. 센서 확인은 STM32가 COMPLETE를 내기 전에 이미 한다. 같은 완료 응답이 반복돼도 COMPLETE 이벤트는 한 번만 발생시킨다.

### 고장 유지

- STM32는 고장을 감지하면 state=FAULT로 바꾸고, 최초 fault 코드를 ARM 성공 전까지 유지한다.
- 고장 후 STOP을 받아도 state=FAULT와 fault 코드를 유지한다. 출력은 이미 정지 상태다.
- FAULT·UNKNOWN 상태에서는 작업 명령을 거부한다. ARM에 성공하면 state=IDLE, fault=0이 된다.
- ARM은 다음을 모두 만족해야 성공한다: 센서 준비, 링크 유효, 센서 동시 감지 없음, MOVING 아님.
- Jetson은 기존처럼 state=FAULT면 고장으로 처리한다.

### 명령 번호와 중복 실행 방지

- Jetson은 축별로 증가하는 16-bit seq를 쓴다. NOP만 0이다.
- NOP를 제외한 모든 요청(STOP·ARM 포함)에 새 seq를 쓴다. 65535 다음은 1이다.
- STM32는 seq 차이를 16비트로 계산해 1~32767이면 새 요청, 그 이상이면 과거 요청으로 판단한다.
- 같은 seq·같은 명령·같은 인자·같은 target이면 다시 구동하지 않고 현재 결과를 반환한다.
- 같은 seq에서 명령·인자·target 중 하나라도 다르면 SEQUENCE로 거부한다. MOVING 중에 들어온 새 작업 명령은 BUSY로 거부한다.
- Jetson은 오류 후 작업 명령을 자동 재전송하지 않는다. STOP은 반복 전송해도 안전하다.
- **재시작 절차:** Jetson 프로세스가 재시작되면 다음 순서로 재개한다. 이전 작업을 자동으로 이어서 하지 않는다.
  1. 각 축에 NOP를 보내 ack_seq를 읽는다.
  2. seq를 ack_seq+1부터 쓴다.
  3. STOP → ARM → RECOVER 순서로 보낸다.
- STM32 재부팅은 stage=15(BOOT)로 확인한다(6장). heartbeat가 작아졌다는 사실만으로는 재부팅을 확정하지 않는다.

## 5. Heartbeat와 제한시간

| 항목 | 기본값 | Jetson 처리 |
| --- | --- | --- |
| X/Z 상태 조회 | 각 100 ms | SPI 태스크가 양축을 번갈아 NOP |
| SPI 태스크 기본 대기 | 10 ms | 빈 루프로 CPU를 소모하지 않음 |
| 유효 응답 제한시간 | 500 ms | magic·version·axis·crc가 정상인 응답이 없으면 오류 |
| 수락 제한시간 | 500 ms | ack_seq 일치 + receipt=0이 없으면 오류 |
| Heartbeat 변화 제한시간 | 1000 ms | counter가 변하지 않으면 오류 |
| `X_MOVE`·`RECOVER` 제한시간 | 30000 ms | 완료 조건을 만족하지 않으면 오류 |
| `PICK`·`PLACE` 제한시간 | 45000 ms | 하강·집게·상승을 합치면 30 s를 넘을 수 있음 |
| 재연결 시도 간격 | 1000 ms | 장치를 다시 열고 감시만 재개 |

- STM32는 메인 루프 안에서 100 ms마다 heartbeat를 증가시킨다. NOP를 받았다는 이유로 증가시키지 않는다.
- Jetson은 두 번째 이후 counter가 변해야 NORMAL로 판단한다. uint16 wraparound(65535→0)는 정상 변화로 본다. 조회 API는 마지막 갱신 시각을 확인한다.
- 이 제한시간은 사용자 태스크가 실행될 때 판정한다. Linux `SPI_IOC_MESSAGE`는 동기 호출이므로, 커널 드라이버가 영구 정지하면 사용자 코드가 정확히 500 ms에 ioctl을 중단시키지 못한다. 하드웨어 watchdog과 드라이버 오류 처리도 별도로 고려한다.

### 호스트 정지 시 MCU 동작

- STOP 명령을 추가한다. 중앙 FSM이 FAULT로 전환하면 Jetson은 관련 축(필요하면 양축)에 STOP을 보낸다.
- **STOP 확인:** ack_seq = STOP seq, receipt = 0, state = UNKNOWN 또는 FAULT
- 유효 요청이 500 ms 동안 없으면 STM32는 스스로 정지한다. NOP가 계속 오는 동안에는 이 자동 정지가 일어나지 않으므로, 정지가 필요하면 반드시 STOP을 보낸다.
- 재가동은 ARM → RECOVER(Z → X) 순서로 한다.
- **ARM 확인:** ack_seq = ARM seq, receipt = 0, state = IDLE, fault = 0
- E84 ES의 별도 안전 회로는 제공된 핀맵의 전제다. SPI 소프트웨어는 이를 대체하지 않는다.

## 6. 중앙 FSM과의 연결

- TScontrol → `TSspi_Set_Axis_Command()`: 명령(ARM/STOP/RECOVER/X_MOVE(정거장)/PICK/PLACE)을 큐에 복사하고 즉시 반환한다. STOP은 우선 처리한다.
- SPI 장치를 열고 송수신하는 것은 SPI 태스크뿐이다.
- `TSspi_Parse()`의 판정 기준:
  - 수락: ack_seq = 요청 seq, receipt = 0
  - 거부: ack_seq = 요청 seq, receipt ≠ 0
  - 완료: 4장 조건
  - 고장: state = FAULT
- 정상 응답이면 상태 캐시(state·position·pos_src 등)를 갱신하고, `TScontrol_Set_Axis_Event()`로 이벤트를 전달한다.
- 통신·Heartbeat·수락·이동 시간 초과는 `TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION)`으로 처리한다.
- 명령 큐가 가득 차면 조용히 덮어쓰지 않고 오류로 전환한다.
- SPI 통신 오류가 나면 작업 명령을 자동 재시도하지 않는다.

### 상대 축 상태 전달 (remote)

X와 Z는 서로 다른 MCU이므로 상대 축 상태를 Jetson이 전달한다. Z가 내려간 상태에서 X가 움직이는 것을 막기 위해서다. 판단은 STM32가 맡는다. 각 축은 상대에게 전달할 값을 직접 계산해 응답 relay 비트(바이트 9 bit0~2)에 넣는다.

| 응답 축 | relay 비트 |
| --- | --- |
| X | bit1 = X 정지, bit2 = X 유효 정거장 |
| Z | bit0 = Z 상단 |

Jetson 처리:

- Z로 보내는 remote = 최신 X 응답의 `바이트 9 & 0x06`
- X로 보내는 remote = 최신 Z 응답의 `바이트 9 & 0x01`
- 상대 축의 유효 응답이 300 ms보다 오래됐으면 remote = 0
- 동작 중에도 NOP를 포함한 모든 요청에 최신 remote를 넣는다

### 상위 시퀀스

- 시작·복구: 양축 ARM → Z RECOVER → X RECOVER
- 반송: Z PICK → X `X_MOVE(1)` → E84 확인 → Z PLACE
- 어느 단계든 실패하면 다음 단계로 진행하지 않고 STOP을 보낸다.

TScontrol의 위치 설정·운전 허용 조건과 TSe84 구현은 이번 SPI 규약의 범위가 아니다.

### 선택 필드

- **sensors:** 현재 센서 비트
- **position / pos_src:** 현재 위치와 출처(2장 위치 필드). GUI 표시 예: "Z 약 60%(추정)"
- **stage:** 현재 진행 중이거나 중단된 작업 단계

| stage | 의미 |
| --- | --- |
| 0 | NONE |
| 1 | X_TO_A |
| 2 | X_TO_B |
| 3 | Z_TO_TOP |
| 4~7 | PICK 하강 / 열기 / 닫기 / 상승 |
| 8~10 | PLACE 하강 / 열기 / 상승 |
| 11 | 점검 명령 |
| 12~14 | 예약(위치 이동 등) |
| 15 | BOOT (부팅 후 ARM 전) |

디코딩:

```c
relay    = b[9] & 0x07U;
stage    = (b[9] >> 3) & 0x0FU;
link     = (b[9] & 0x80U) != 0U;
position = (int32_t)(((uint32_t)b[12] << 24) | ((uint32_t)b[13] << 16) |
                     ((uint32_t)b[14] << 8)  |  (uint32_t)b[15]);
pos_src  = b[16];
hb       = (uint16_t)((b[18] << 8) | b[19]);
```

## 7. IIO 검토 결과

Linux IIO는 ADC/DAC/센서 등의 채널과 속성·버퍼를 다루는 프레임워크다. SPI 하드웨어 위에 IIO 드라이버를 작성할 수는 있지만, IIO가 임의의 SPI 프레임을 직접 송수신하는 범용 사용자 API는 아니다.

이 프로토콜을 IIO로 노출하려면 별도 커널 드라이버와 장치 모델 설계가 필요하다. 현재는 사용자 영역에서 spidev + `ioctl(SPI_IOC_MESSAGE)`로 MCU와 프로토콜을 주고받는다. 새 라이브러리 설치나 IIO 커널 드라이버를 필요로 하지 않는다.

근거:

- Linux IIO core
- Linux SPI userspace API


## 9. Linux API와 MISRA 적용 범위

배열 경계, 고정 폭 정수, 명시적인 바이트 순서 변환, NULL 검사, 큐 동기화, 제한시간 처리를 적용했다. 전용 MISRA 검사기의 적합성 인증은 수행하지 않았다.

`spi_ioc_transfer.tx_buf/rx_buf`가 정수형 주소를 요구하므로, uintptr_t를 경유하는 포인터→정수 변환을 `TSspi_Set_Transfer()`에 한정했다. ioctl의 가변 인수, Linux UAPI 구조체, POSIX clock/open/close/nanosleep, pthread도 플랫폼 의존 지점이다. 이는 spidev 사용자 API를 쓰기 위한 추가 편차다. 일반 프레임 인코딩에서는 구조체 포인터 캐스팅이나 구조체 전체 복사를 사용하지 않는다.

## 10. TSspi 변경 요약

**그대로 유지하는 것:**

- 축 번호(X=1, Z=2), big endian, state 코드 순서
- NOP 기반 2단계 송수신, 1 ms 응답 대기, 100 ms 조회
- 태스크·큐 구조, 자동 재전송 금지, 중복 완료 억제
- 제한시간 체계, fault≠0 처리
- signed 32-bit 위치 필드

**바꿔야 하는 것과 이유:**

| 항목 | 변경 | 이유 |
| --- | --- | --- |
| 프레임 | 28바이트 → 24바이트, 필드 재배치 | STM32 규약 기반 + 위치·예약 공간 |
| 체크섬 | 바이트 합산 → CRC-16 | 바이트 순서 오류 검출. 참조 코드는 STM32 측 제공 |
| 명령 | MOVE(position) → RECOVER/X_MOVE/PICK/PLACE | 현재 엔코더가 없고, PICK/PLACE 구분이 필요 |
| 정지·재가동 | STOP·ARM 추가 | 오류 시 모터를 세울 수단이 필요 |
| 상대 축 전달 | relay 비트를 remote로 전달 | X·Z 상호 인터락 |
| 완료 판정 | 위치 일치 → op_seq + COMPLETE + fault=0 | 수치 위치를 정확히 알 수 없음 |
| 수락 판정 | ACK 메시지 → ack_seq + receipt | 응답 형식을 하나로 통합 |
| 위치 해석 | 응답 position + pos_src(‰ 단위) | 추정·센서 확인·실측을 구분 |
| heartbeat·seq | uint32 → uint16 | 24바이트에 맞춤. 비교 폭만 변경 |
| PICK/PLACE 제한시간 | 45 s 추가 | 동작이 30 s를 넘을 수 있음 |

## 11. 확장 규칙

24바이트 프레임에는 아래 확장을 프레임 변경 없이 넣을 수 있다.

| 확장 | 방법 |
| --- | --- |
| 정거장 추가 | `X_MOVE`의 arg에 정거장 번호 2, 3…을 쓴다. 센서와 펌웨어 정거장 표만 추가한다 |
| 엔코더 추가 | 응답 position에 실측값을 넣고 pos_src=3으로 보고한다. 단위(‰)는 그대로다 |
| 위치 이동 명령 | 예약 cmd 17 `X_MOVE_POS`, 33 `Z_MOVE_POS`. 요청 target(‰)으로 이동한다. 실측 위치(pos_src=3)를 지원하는 펌웨어에서만 수락하고, 그 외에는 receipt=COMMAND |
| 속도·옵션 | 요청 option(uint16)을 쓴다 |
| 진단 정보 | 응답 바이트 17, 20~21 예약 공간을 쓴다. 모자라면 페이지 조회 명령(`GET_INFO`)을 추가한다 |

**명령 번호 영역:**

| 범위 | 용도 |
| --- | --- |
| 0~15 | 제어 |
| 16~31 | X축 |
| 32~47 | Z축·집게. 기존 점검 명령 32~35 포함 |
| 48~63 | 진단·조회 |

**version 규칙:**

- 예약 필드에 새 의미를 부여하는 것은, 그 필드가 0일 때 기존 동작과 같다면 version을 유지한다.
- 기존 필드의 의미·위치·단위를 바꾸면 version을 올린다.
- 모르는 version의 프레임은 폐기한다.

**미확정 항목:** SPI 1 MHz 동작 여부(실물 확인), 제한시간·보정값 실측, mm 단위 위치가 필요한지 여부.