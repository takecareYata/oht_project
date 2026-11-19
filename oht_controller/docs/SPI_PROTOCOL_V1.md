# Jetson–STM32 SPI 통신 규약 및 STM32 펌웨어 구현 사항

이 문서는 `TSspi.h` / `TSspi.c`에 구현된 **제안 규약**이다. 기존 STM32 펌웨어의 호환성을 확인한 규격이 아니다. 양쪽 펌웨어가 아래 프레임과 교환 순서를 동일하게 구현해야 한다. 위치 단위·이동 한계·모드·속도·대기시간은 실제 장비에서 합의하고 검증한다.

## 1. 연결과 기본 설정

| 항목 | 현재 구현 |
|---|---|
| Jetson 역할 | SPI master |
| STM32 역할 | SPI slave, X/Z 각각 별도 MCU |
| 공유 배선 | 물리 핀 19 MOSI / 21 MISO / 23 SCLK |
| X축 | 물리 핀 24, CS0, STM32②, `/dev/spidev0.0` |
| Z축 | 물리 핀 26, CS1, STM32①, `/dev/spidev0.1` |
| 기본 모드 | Mode 0: CPOL=0, CPHA=0 |
| 데이터 | MSB first, 8 bit/word, 다중 바이트 정수는 big endian |
| 초기 속도 | 1 MHz (`TSspi_SPEED_HZ`) |
| CS | active low. 프레임 28바이트 동안 유지, 프레임 사이 해제 |
| 응답 준비 대기 | 요청 CS 해제 후 최소 1 ms (`TSspi_RESPONSE_DELAY_US`) |

장치 경로는 제공된 핀맵 기준이다. 실제 Jetson의 pinmux/device tree 설정과 `/dev/spidev*` 번호를 확인해야 한다. 이 프로그램은 pinmux, device tree, 장치 권한을 변경하지 않는다. 모드 0~3을 설정해 MSB-first, active-low CS를 사용한다.

## 2. 고정 길이 프레임

모든 요청·응답은 **28바이트**이다. C 구조체 전체를 `memcpy`하거나 `sizeof(struct)`로 전송하지 않는다. 아래 오프셋에 맞춰 명시적으로 인코딩/디코딩한다.

| 바이트 오프셋 | 크기 | 필드 | 설명 |
|---:|---:|---|---|
| 0 | 2 | magic | `0xA55A` |
| 2 | 2 | msgid | 명령/응답 종류 |
| 4 | 4 | seq | 요청과 응답을 연결하는 번호. 0은 NOP 전용 |
| 8 | 2 | len | 항상 16. state부터 axis까지의 크기 |
| 10 | 2 | state | 아래 축 상태 코드 |
| 12 | 4 | position | signed 32-bit, 2의 보수. MOVE는 목표, 응답은 현재 위치 |
| 16 | 4 | heartbeat | STM32에서 주기적으로 증가시키는 uint32 counter |
| 20 | 2 | fault | 0: 정상, 그 외: STM32 오류 코드 |
| 22 | 2 | flags | bit0=해당 MOVE 수락, 나머지 bit=0 |
| 24 | 2 | axis | X=1, Z=2. 실제 선택된 CS와 일치해야 함 |
| 26 | 2 | crc | 바이트 0~25의 합계 & `0xFFFF` |

`crc`라는 필드명을 쓰지만 현재 계산은 TCP와 같은 **단순 바이트 합산 체크섬**이다. CRC 다항식 연산이 아니며 바이트 순서 교환 같은 오류를 모두 검출하지 못한다. 향후 CRC-16으로 변경한다면 양쪽 구현과 규약 버전을 함께 변경해야 한다. 현재 프레임에는 별도 버전 필드가 없다.

부호 있는 위치값의 예: `-100`은 `FF FF FF 9C`, `INT32_MIN`은 `80 00 00 00`이다. 위치 단위/원점/방향/허용 범위는 미확정이므로 STM32는 실제 기구 사양에 따라 유효 범위를 검증해야 한다.

### 메시지 ID

| msgid | 이름 | 방향 | payload 의미 |
|---|---|---|---|
| `0x0000` | NOP | Jetson → STM32 | 응답을 읽기 위한 클록. seq=0, axis만 설정 |
| `0x0010` | MOVE | Jetson → STM32 | position=목표 위치, seq=새 이동 번호 |
| `0x0011` | ACK | STM32 → Jetson | 이동 명령 수락, flags bit0=1 |
| `0x0012` | GET_STATUS | Jetson → STM32 | 상태 조회. 이동 중에는 진행 중 MOVE의 seq 사용 |
| `0x0013` | STATUS | STM32 → Jetson | 현재 상태/위치/Heartbeat/오류 |
| `0x0014` | COMPLETE | STM32 → Jetson | 실제 이동 완료. state=COMPLETE, flags bit0=1 |
| `0xFFFF` | ERROR | STM32 → Jetson | 요청 거부 또는 장치 오류. fault에 원인 코드 |

NOP를 제외한 요청은 사용하지 않는 payload 필드를 0으로 채운다. 모든 프레임의 magic, len, axis, checksum은 채운다. NOP도 같은 형식과 checksum을 사용한다.

### state 코드

| 값 | 상태 |
|---:|---|
| 0 | UNKNOWN |
| 1 | IDLE |
| 2 | MOVING |
| 3 | COMPLETE |
| 4 | FAULT |

현재 Jetson은 STM32의 상세 fault 코드가 0이 아니면 모두 `main_FAULT_SPI_COMMUNICATION`으로 중앙 FSM에 알린다. 상세 오류 분류를 상위 GUI까지 전달하려면 후속 인터페이스 확장이 필요하다.

## 3. SPI는 요청 전송과 응답 수신을 두 번으로 나눈다

SPI slave는 master가 클록을 제공할 때만 송신할 수 있다. 또한 MOVE 프레임을 받기도 전에 그 MOVE의 ACK를 준비할 수는 없다. 따라서 아래 순서를 따른다.

1. STM32는 RX/TX DMA 버퍼를 미리 준비한다.
2. Jetson이 CS를 내리고 요청 28바이트를 전송한 뒤 CS를 해제한다. 이때 MISO에서 읽힌 이전 응답은 버린다.
3. STM32는 요청 수신 완료를 확인하고 검증·해석한다. 다음 전송용 응답 28바이트를 준비한다.
4. Jetson은 최소 1 ms 기다린다. Linux 스케줄링에 따라 실제 대기는 더 길 수 있다.
5. Jetson이 같은 CS로 NOP 28바이트를 전송하며 현재 응답 28바이트를 읽는다.
6. STM32는 NOP를 새 MOVE/조회로 취급하지 않는다. NOP 자체에 대한 ACK를 만들거나 명령 번호를 0으로 덮어쓰지 않는다.

각 CS 구간은 정확히 한 프레임이다. 짧거나 긴 전송, CS 중도 해제, SPI overrun 발생 시 해당 수신은 버리고 다음 CS에서 프레임 경계를 복구한다. STM32는 CS 해제 후에도 다음 전송을 받을 준비를 유지해야 한다.

응답 준비가 1 ms를 넘을 수 있으면 `TSspi_RESPONSE_DELAY_US`를 실측해 늘린다. Jetson은 오래된 seq 응답을 무시하지만, 현재 MOVE 요청의 read 단계가 손상되면 명령 실행 여부가 불명확하므로 MOVE를 재전송하지 않고 오류로 전환한다.

### DMA 구현 시 고려할 사항

- SPI/DMA 설정, CS 처리 방법은 사용하는 STM32 시리즈와 HAL/LL 버전에 맞춰 구현한다.
- CS active 전에 TX/RX DMA를 준비한다. 콜백에서 응답 데이터를 만들기 시작하면 그 전송에는 늦을 수 있다.
- DMA가 사용하는 TX 버퍼를 작성 중 덮어쓰지 않는다. 필요하면 이중 버퍼로 응답을 교체한다.
- D-cache가 있는 STM32는 사용 메모리의 DMA 접근 가능 여부, 정렬, cache clean/invalidate를 확인한다.
- SPI ISR/완료 콜백 안에서 모터 이동 완료를 기다리지 않는다. 검증된 요청을 제어 태스크에 전달하고 빠르게 복귀한다.
- 명령 수락 상태를 일관되게 갱신한 뒤 ACK를 게시한다. 물리 동작이 끝날 때까지 SPI 상태 조회에 계속 응답한다.

## 4. MOVE 처리와 실제 동작 완료

STM32 측 처리 흐름은 다음과 같다.

1. magic/len/checksum/axis/명령/목표 위치를 검증한다.
2. 새 seq인지 확인하고, 현재 축의 인터록·이동 가능 여부를 확인한다.
3. 명령을 수락하면 해당 seq와 목표를 저장한다. state를 MOVING으로 바꾸고 flags bit0=1인 ACK를 준비한다.
4. 모터 제어 태스크가 비동기로 이동을 수행한다.
5. 센서/엔코더/드라이버 상태로 실제 완료를 확인한다. 목표 카운트에 도달했다는 이유만으로 기계적인 완료 조건이 충족된 것으로 간주하지 않는다.
6. state=COMPLETE, position=실제 위치인 응답을 준비한다. 이후 GET_STATUS에도 이 완료 상태를 유지한다.
7. **새 MOVE가 수락될 때** 이전 완료 상태를 해제한다. ACK 송신이나 단 한 번의 조회만으로 완료 상태를 지우지 않는다.

현재 Jetson은 다음 조건을 모두 만족해야 중앙 FSM으로 COMPLETE 이벤트를 전달한다.

- 현재 대기 중인 MOVE가 있음
- 응답 axis와 실제 CS가 일치함
- 응답 seq가 대기 중인 MOVE seq와 일치함
- ACK 메시지 또는 flags bit0를 통해 명령 수락이 확인됨
- state=COMPLETE
- 현재 위치가 명령 목표 위치와 정확히 일치함
- ACK/이동/응답/Heartbeat 제한시간을 넘지 않음

물리적인 완료 허용 오차가 필요하면 양쪽의 위치 비교 규칙을 함께 변경해야 한다. 현재는 오차 범위를 두지 않는다. 조건 만족 후 Jetson은 같은 완료 응답이 반복돼도 COMPLETE 이벤트를 한 번만 발생시킨다. COMPLETE 메시지를 놓쳐도 STATUS의 완료 상태로 검출할 수 있다.

### 명령 번호와 중복 실행 방지

- Jetson은 축별로 증가하는 32-bit seq를 사용한다. 0을 사용하지 않으며 최대값 도달 시 오류로 전환한다.
- 이동 대기 중 GET_STATUS는 해당 MOVE seq를 그대로 사용한다. ACK를 먼저 못 받았으면 STATUS의 flags bit0=1로도 수락을 확인한다.
- 이동 대기 중이 아닐 때 조회는 새 seq를 사용한다. 이 경우 STATUS는 조회 seq를 반영하되, 조회가 STM32의 마지막 MOVE 기록을 바꾸면 안 된다.
- STM32는 마지막으로 수락한 MOVE seq와 목표를 저장한다. **같은 seq/같은 목표의 MOVE는 다시 구동하지 않고 현재 결과를 반환**한다.
- 같은 seq인데 목표가 다르거나, 진행 중인 축에 다른 MOVE가 들어오면 거부하고 ERROR를 반환한다.
- 늦은 이전 MOVE의 응답에 새로운 MOVE의 seq를 붙이면 안 된다. 새 MOVE 수락 시 상태·목표·seq를 한 번에 갱신한다.
- Jetson은 오류 후 MOVE를 자동 재전송하지 않는다. CS 통신 재개 후에도 MOVE 금지 상태는 프로세스 재시작까지 유지한다.

**재시작 제약:** 현재 프레임에는 session/boot ID나 HELLO 동기화 절차가 없다. Jetson 프로세스가 재시작되면 seq가 다시 시작하므로, 양산 적용 전 세션 동기화 절차가 필요하다. 현 버전에서 단순히 Jetson만 재시작하고 이전 STM32 명령 캐시를 그대로 사용하는 것은 지원하지 않는다. 양축의 정지·상태 확인 후 양측 명령 번호를 동기화하는 운영 절차 또는 후속 프로토콜 확장을 정해야 한다. STM32 재부팅은 fault나 boot counter로 명시적으로 보고해야 하며, heartbeat counter가 작아졌다는 사실만으로 재부팅을 확정할 수는 없다.

## 5. Heartbeat와 제한시간

| 항목 | 기본값 | Jetson 처리 |
|---|---:|---|
| X/Z 상태 조회 | 각 100 ms | SPI 태스크가 양축을 번갈아 GET_STATUS |
| SPI 태스크 기본 대기 | 10 ms | 빈 루프로 CPU를 소모하지 않음 |
| 유효 응답 제한시간 | 500 ms | 정상 magic/len/checksum/axis/seq 응답이 없으면 오류 |
| MOVE 수락 제한시간 | 500 ms | ACK/수락 flag가 없으면 오류 |
| Heartbeat 변화 제한시간 | 1000 ms | counter가 변하지 않으면 오류 |
| 실제 MOVE 제한시간 | 30000 ms | 완료 조건을 만족하지 않으면 오류 |
| 재연결 시도 간격 | 1000 ms | 장치 재열기. 감시만 재개 |

STM32 heartbeat는 독립적인 주기 작업에서 증가시킨다. **GET_STATUS 요청을 받았다는 이유만으로 증가시키면 모터 제어 태스크 정지를 탐지할 수 없다.** 어떤 태스크의 생존을 대표하는 counter인지 펌웨어에서 정한다. 증가 주기는 1000 ms보다 충분히 짧게 설정하며 정확한 값은 양쪽에서 합의한다.

Jetson은 첫 counter만으로 정상으로 판단하지 않는다. 두 번째 이후 counter의 변화를 봐야 NORMAL이 된다. uint32 wraparound는 정상 변화로 취급한다. 조회 API도 마지막 갱신 시각을 확인하므로 SPI 태스크가 멈춘 뒤 오래된 NORMAL을 계속 반환하지 않는다.

이 제한시간은 사용자 태스크가 실행될 때 판정한다. Linux `SPI_IOC_MESSAGE`는 동기 호출이므로 커널 드라이버 자체가 영구 정지한 경우 사용자 코드가 정확히 500 ms에 ioctl을 중단시키지는 못한다. 하드웨어 watchdog과 드라이버 오류 처리도 별도로 고려한다.

### 호스트 정지 시 MCU 동작

현재 규약에 STOP/ABORT 명령과 정지 확인 응답은 없다. 중앙 FSM의 FAULT 전환은 실제 모터 정지 명령을 보냈다는 뜻이 아니다. STM32는 호스트의 주기 조회 단절, 자체 센서/드라이버 오류, 비상 정지 신호에 대한 로컬 정지 정책을 가져야 한다. 구체적인 정지 방식과 허용 시간은 장비 사양으로 정한다. E84 ES의 별도 안전 회로는 제공된 핀맵의 전제이며 SPI 소프트웨어가 이를 대체하지 않는다.

## 6. 중앙 FSM과의 연결

- `TScontrol` → `TSspi_Set_Axis_Command()`: 명령을 큐에 복사하고 즉시 반환한다.
- SPI 태스크만 장치를 열고 SPI 전송/수신을 수행한다. 다른 태스크는 장치 FD에 직접 접근하지 않는다.
- `TSspi_Parse()`는 ACK/STATUS/COMPLETE/ERROR를 switch로 구분한다.
- 정상 응답: 상태 캐시 갱신 후 `TScontrol_Set_Axis_Event()`로 이벤트 전달.
- 통신/Heartbeat/ACK/이동 시간초과: `TScontrol_Set_Fault(main_FAULT_SPI_COMMUNICATION)`.
- 명령 큐가 가득 차면 조용히 덮어쓰지 않고 오류로 전환한다.
- SPI 통신 오류가 나면 자동 MOVE 재시도를 하지 않는다. 이미 실행한 명령의 중복 동작을 막기 위해서다.
- `TSe84` 구현과 `TScontrol_POSITIONS_CONFIGURED`/목표 위치 설정은 이번 SPI 구현에서 변경하지 않았다. 따라서 SPI가 정상이어도 E84가 미구현이거나 위치 설정이 없으면 중앙 FSM은 운전을 허용하지 않는다.

## 7. IIO 검토 결과

Linux IIO는 ADC/DAC/센서 등의 채널과 속성·버퍼를 다루는 프레임워크이다. SPI 하드웨어 위에 IIO 드라이버를 작성하는 것은 가능하지만, IIO가 임의의 SPI 프레임을 직접 송수신하는 범용 사용자 API는 아니다.

이번 STM32 MOVE/ACK/COMPLETE 프로토콜을 IIO로 노출하려면 별도 커널 드라이버와 장치 모델 설계가 필요하다. 현재는 사용자 영역에서 MCU와 프로토콜을 주고받는 `spidev` + `ioctl(SPI_IOC_MESSAGE)`를 사용한다. 새 라이브러리 설치나 IIO 커널 드라이버를 필요로 하지 않는다.

근거:

- [Linux IIO core](https://docs.kernel.org/driver-api/iio/core.html)
- [Linux SPI userspace API](https://docs.kernel.org/spi/spidev.html)

## 8. 검증과 실물 적용 순서

모의 장치 통합 시험은 SPI 장치 파일을 실제로 열지 않는다. `open/ioctl/clock/nanosleep`을 테스트 코드로 대체하며, 실제 `TScontrol`과 `TSspi` 구현을 함께 실행한다.

`oht_controller` 폴더에서:

```sh
gcc -std=c99 -Wall -Wextra -Werror -pthread -fsanitize=undefined \
    test/test_tsspi.c \
    -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl \
    -Wl,--wrap=clock_gettime -Wl,--wrap=nanosleep \
    -o /tmp/test_tsspi
/tmp/test_tsspi
make -C app
make -C app check
```

시험 항목: X/Z 장치 선택, 프레임/부호 있는 위치/합산 checksum, ACK와 실제 완료 구분, 전체 중앙 FSM 순서, 중복 완료 억제, 잘못된 seq/axis/checksum, 짧은 전송, 응답/ACK/Heartbeat/MOVE timeout, 큐 포화, 장치 부재, 재접속 후 MOVE 재전송 금지.

실물에서는 먼저 장치 노드/pinmux를 확인하고, MOVE 없이 GET_STATUS/NOP 및 Heartbeat를 로직 애널라이저로 확인한다. 다음으로 각 축을 개별 시험하고, ACK→MOVING→COMPLETE와 목표 위치를 확인한다. 마지막으로 중앙 FSM/E84 통합을 시험한다. 이 작업에서는 실물 SPI 트랜잭션을 실행하지 않았다.

## 9. Linux API와 MISRA 적용 범위

배열 경계, 고정 폭 정수, 명시적인 바이트 순서 변환, NULL 검사, 큐 동기화, 제한시간 처리를 적용했다. 전용 MISRA 검사기의 적합성 인증은 수행하지 않았다.

`spi_ioc_transfer.tx_buf/rx_buf`가 정수형 주소를 요구하므로 `uintptr_t`를 경유하는 포인터→정수 변환을 `TSspi_Set_Transfer()`에 한정했다. `ioctl`의 가변 인수, Linux UAPI 구조체, POSIX clock/open/close/nanosleep 및 pthread 역시 플랫폼 의존 지점이다. 이는 spidev 사용자 API를 사용하기 위한 추가 편차이며, 일반 프레임 인코딩에서는 구조체 포인터 캐스팅이나 구조체 전체 복사를 사용하지 않는다.
