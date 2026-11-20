# SPI v2 구현 및 STM32 연동

현재 [TSspi.c](../app/TSspi.c), [TSspi.h](../app/TSspi.h), [TScontrol.c](../app/TScontrol.c)는 [제안 규약 v2](제안_SPI_PROTOCOL.md)를 적용한다. 과거 28바이트 규약은 [v1 기록](SPI_PROTOCOL_V1.md)으로 보관한다. v1 펌웨어와 호환되지 않는다.

## 연결과 명령

Jetson master / STM32F411RE slave. MOSI=물리19, MISO=21, SCLK=23. X CS0=24(`/dev/spidev0.0`), Z CS1=26(`/dev/spidev0.1`). Mode0, MSB first, 8bit, 1MHz 기본값. pinmux/device tree와 권한은 프로그램이 변경하지 않는다.

| cmd | 명령 | 축 | 인자 및 동작 |
|---|---|---|---|
| 0 | NOP | X/Z | seq=0. 조회만 하며 ack_seq/op_seq를 바꾸지 않음 |
| 1 | ARM | X/Z | STOP 확인 후 제어 허용·MCU 고장 해제 |
| 2 | STOP | X/Z | 일반 큐를 비우고 우선 처리, ARM 해제 |
| 3 | RECOVER | X/Z | Z 상단 복구 후 X A 복구 |
| 16 | X_MOVE | X | arg=정거장, 0=A / 1=B |
| 36 | PICK | Z | 하강·집게 열기·닫기/접촉 확인·상승 |
| 37 | PLACE | Z | 파지 확인·하강·열기/해제 확인·상승 |

현재 target/option=0. arg는 X_MOVE 외에는 0. 예약된 위치 이동 명령은 구현하지 않는다. 요청의 유효성·실제 구동·센서 인터록·중복 실행 억제는 STM32에서도 구현해야 한다.

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

## 응답 처리와 재전송

- `TSspi_Set_Exchange`: 요청 24바이트 → CS 해제 → 최소 1ms → NOP 24바이트. 첫 전송의 동시 RX는 사용하지 않는다.
- `TSspi_Set_Transfer`는 모든 전송 전에 최소 1ms 대기한다. 따라서 NOP 응답을 읽은 직후 같은 축에 새 요청을 보내도 CS HIGH 시간을 확보한다. STM32 SPI2 DMA의 NSS 상승 후 재준비 조건에 맞춘 처리다.
- 수락은 `ack_seq == 요청 seq && receipt == 0`. 반영이 늦으면 500ms 안에서 NOP로 기다린다. 거부 receipt는 즉시 고장이다.
- 작업 완료는 수락 조건과 `op_seq == 요청 seq`, `state=COMPLETE`, `fault=0`. position은 완료 조건에 사용하지 않는다.
- ARM 확인은 해당 ack_seq, receipt=0, IDLE, fault=0. STOP 확인은 해당 ack_seq, receipt=0, UNKNOWN 또는 FAULT이다. STOP 응답의 기존 fault는 허용한다.
- 일반 작업은 재전송하지 않는다. STOP은 동일 seq로 반복한다. 새 비NOP 명령마다 uint16 seq를 증가시키고 65535 다음은 1을 사용한다.
- 처음 받은 유효 NOP 응답의 ack_seq로 번호를 동기화한다. STOP이 필요한데 NOP 응답을 검증할 수 없으면 보유 번호로 STOP 송신을 시도한다. STM32가 STOP의 seq 검사를 우회해야 한다. 확인 응답 없이는 정지 완료로 처리하지 않는다.
- 송수신은 SPI 태스크만 담당한다. `TSspi_Parse`에는 바이트 검증이 끝난 응답 구조체를 전달한다. 다른 태스크에서는 큐/캐시 API를 사용한다.

## 시작·고장·복구와 중앙 FSM

**프로세스 시작 시 실제 복구 이동이 발생할 수 있다.** 이전 작업을 재개하지 않고 양축 NOP 동기화 → 양축 STOP 확인 → ARM Z → ARM X → RECOVER Z → RECOVER X → IDLE 순서로 처리한다.

운전은 Z PICK → X X_MOVE(1) → E84 준비 → Z PLACE → PLACE 완료 및 L_REQ OFF → E84 완료 → 작업 완료이다. PICK/PLACE에 상승이 포함되므로 별도 Z_RETURN 명령은 없다. 기존 상태 번호 7은 호환을 위해 남겨두지만 사용하지 않는다.

고장 시 양축 STOP을 우선 요청하고 일반 큐를 지운다. 장치를 1초 뒤 다시 열어 감시와 STOP을 시도하지만 ARM이나 작업을 자동 재실행하지 않는다. 정지 통신 자체가 불가능하거나 ioctl이 멈추면 소프트웨어로 물리 정지를 보장할 수 없다. STM32의 유효 요청 500ms 단절 시 로컬 정지 및 별도 ES 안전 회로가 필요하다.

TCP 프레임 길이와 msgid는 유지한다. GET_COMMAND payload의 [main_command_t](../app/main.h)는 START=1, STOP=2, RECOVER=3이다. STOP은 중앙 FAULT_STOP=5로 유지하며, RECOVER는 INIT/IDLE/COMPLETE/FAULT에서만 접수한다. 복구 중 기존 중앙 fault는 유지하고 양축 RECOVER 완료 후 해제한다. 명시적 RECOVER 완료는 SET_COMPLETE payload=3, 시작 시 내부 복구는 완료 알림을 보내지 않는다.

STATUS.state에 STOPPING=10, ARMING_Z=11, ARMING_X=12, RECOVER_Z=13, RECOVER_X=14를 추가했다. 기존 0~9 값은 유지한다.

START에는 중앙 IDLE/COMPLETE·고장 없음·양축 ARM·IDLE/COMPLETE·정상 SPI Heartbeat가 필요하다. E84 Heartbeat 설정·조회·운전 조건은 제거했다. 조건을 만족하면 START로 Z PICK과 X 이동을 진행한다. [TSe84.c](../app/TSe84.c)는 아직 GPIO 처리 골격이므로 이후 E84 준비 단계는 실제 READY 이벤트가 올 때까지 기다린다. 이 파일의 E84 시퀀스는 호출 인터페이스 연결이며 GPIO 구현 완료를 뜻하지 않는다.

## 상태 전달 및 제한시간

| 항목 | 구현 기본값 |
|---|---|
| 루프 / 양축 NOP 조회 | 10ms / 각 100ms |
| 유효 응답 / 명령 접수 | 각 500ms |
| Heartbeat 변화 | 1000ms. 첫 값은 UNKNOWN, 이후 변화 시 NORMAL. uint16 순환 허용 |
| X_MOVE / RECOVER | 최초 요청부터 30s |
| PICK / PLACE | 최초 요청부터 45s |
| 상대축 응답 신선도 | 300ms 초과 시 remote=0 |
| 장치 재열기 | 1s |

X로 remote=`Z relay & 0x01`, Z로 remote=`X relay & 0x06`. NOP를 포함한 모든 요청에 넣는다. CRC·헤더·축이 정상인 최신 응답을 사용하고, **상대축 link bit7이 꺼져도 remote=0**으로 처리한다. ARM 이후 stage=15(BOOT)이 보이면 MCU 재부팅으로 고장 처리한다.

state는 UNKNOWN=0, IDLE=1, MOVING=2, COMPLETE=3, FAULT=4. receipt/fault는 OK=0, STOP=1, LINK=2, SENSOR=3, TIMEOUT=4, INTERLOCK=5, GRIP=6, BUSY=7, COMMAND=8, SEQUENCE=9, TRANSPORT=10이다.

MCU의 15s 이동 제한이 PICK/PLACE 각 구간인지 전체인지 실물 사양에서 확정해야 한다. 호스트는 제안서대로 작업 전체 45s를 적용한다. MCU의 구동 시간·센서 해제·DMA·MISO high-Z 등은 [제안서](제안_SPI_PROTOCOL.md)의 STM32 구현 항목을 따른다.

## 빌드와 장치 없는 시험

프로젝트 루트에서 실행한다.

```sh
make -C app
make -C app check
make -C app test
```

check는 GCC 정적 분석 빌드이며 프로그램을 실행하지 않는다. test는 [모의 통합 시험](../test/test_tsspi.c)으로 open/close/ioctl/clock_gettime/nanosleep을 교체한다. 실물 장치를 열지 않고 CRC 벡터, 24바이트 인코딩, 예약 필드, 지연 접수, op_seq 불일치, PICK/PLACE, E84 순서, 중복 완료, STOP 우선순위·확인 실패, 수락/작업/Heartbeat 시간초과, CRC·축·짧은 전송·I/O 오류, seq/heartbeat 순환, BOOT·명시적 복구, remote 만료를 확인한다. E84는 정상 모형을 사용한다.

실제 STM32 펌웨어·파형·구동부 검증은 별도이며, 해당 시험 결과는 실물 검증을 대신하지 않는다.

## SPI 디버그 출력

[TSspi.h](../app/TSspi.h)에서 전처리로 제어한다.

| 설정 | 기본값 | 의미 |
|---|---|---|
| TSspi_DEBUG_ENABLED | 1 | 초기화·명령·오류 진단 활성화. 0이면 기록/인수 평가/출력 스레드를 전부 제거 |
| TSspi_DEBUG_FRAMES_ENABLED | 0 | 1이면 각 TX/RX 24바이트 HEX도 기록. DEBUG_ENABLED=1일 때만 적용 |

장치 열기·SPI ioctl 설정 실패(errno), seq 동기화, Heartbeat 정상 전환, 명령 시작/접수/완료, STOP/ARM 확인, MCU 거부·고장·BOOT, 수신 CRC/필드/축 불일치, 응답·Heartbeat·접수·작업 시간초과를 구분한다. 기본 설정에서는 정상 NOP/STATUS를 매번 출력하지 않는다. 잘못된 응답은 기본 설정에서도 24바이트 HEX를 기록한다.

출력은 stderr이며 `TSspi X ...` 또는 `TSspi Z ...`로 축을 구분한다. 모든 숫자는 16진수다. `detail`은 메시지에 따라 errno, 시간 제한(ms), 기대 CRC 또는 수신 축 번호다. cmd/seq는 당시 요청, ack/op/state/receipt/fault/hb는 수신 상태 캐시의 스냅샷이다. 연결 장치는 X=/dev/spidev0.0, Z=/dev/spidev0.1이다.

SPI 태스크에서는 문자열 포맷팅과 write를 하지 않는다. 고정 크기 64개 큐에 trylock으로 기록하며, 잠금 경합 시 즉시 버리고 큐 포화 시 덮어쓰지 않는다. 큐 포화로 버린 개수는 나중에 보고한다. 별도의 분리된 출력 스레드가 최대 16개씩 가져와 잠금을 해제한 뒤 출력하고 100ms 대기한다. stderr 출력이 막혀도 SPI 태스크가 기다리지 않는다. 출력 스레드 생성에 실패하면 진단이 비활성화되고 제어는 계속한다. 출력 스레드는 SIGPIPE를 차단하며 출력 오류는 제어 fault를 바꾸지 않는다.

기본 네 CSU 태스크에 진단용 보조 스레드 하나가 추가된다. 로그는 비동기이므로 TCP 로그와 발생 순서대로 섞여 보이지 않을 수 있다. 활성화 시 큐 복사와 스케줄링 비용은 존재하며, 실물에서 타이밍 영향이 0임을 보장하지 않는다. DEBUG_ENABLED=0에서는 진단 코드가 제거되며, 해당 빌드의 SPI .text가 변경 전과 동일함을 확인했다.

컴파일 옵션을 바꿀 때는 기존 오브젝트가 남지 않도록 재빌드한다.

```sh
# 모든 SPI 디버그 끄기
make -B -C app CPPFLAGS="-DTSspi_DEBUG_ENABLED=0"
# 전체 TX/RX 추적 켜기
make -B -C app CPPFLAGS="-DTSspi_DEBUG_FRAMES_ENABLED=1"
# 기본값(핵심 진단만 활성)으로 복귀
make -B -C app
```

`make -C app test`는 [진단 큐 시험](../test/test_spi_debug.c)도 실행한다. RX 스냅샷 보존, 큐 경합·포화, 부분 쓰기·EINTR, 출력 차단 중 생산자 진행, 출력 오류 격리를 검사한다. 기존 SPI/FSM 시험도 진단 기록을 활성화하여 실행하며, 실제 출력 스레드나 하드웨어는 시작하지 않는다. 실물 프로그램으로 실행하면 초기 복구 이동이 발생할 수 있다는 기존 조건은 동일하다.
