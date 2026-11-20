# OHT LoadPort — STM32F411RE 베어메탈 펌웨어

STM32①은 12V Z축 액추에이터(L298N)와 SG90 집게를, STM32②는 X축 바이폴라 스텝모터(TB6600 STEP/DIR 드라이버)를 담당합니다. CMSIS 레지스터 정의를 사용하며 HAL/LL 및 RTOS는 사용하지 않습니다. 두 보드 모두 SPI Slave이고 Jetson이 전체 공정을 조정합니다.

**현재 제공물은 빌드 가능한 펌웨어와 호스트 시뮬레이션 테스트입니다. 이 핀맵으로 실제 보드에 다운로드하거나 모터·SPI 파형을 검증한 것은 아닙니다. 기존 배선 정보가 없어 아래 핀맵을 새로 지정했습니다.** 사용자에게서 확인된 사항은 기존 구성으로 X/Z 모터가 작동했다는 점입니다.

## 빌드

필수 도구: `arm-none-eabi-gcc`, GNU Make, Python 3. 호스트 테스트에는 별도 PC용 `gcc`가 필요합니다.

```sh
make both
make test
```

- `build/board1/firmware.elf`, `.bin`: Z축·집게
- `build/board2/firmware.elf`, `.bin`: X축
- `make BOARD=1`, `make BOARD=2`: 개별 빌드
- ST-Link와 STM32CubeProgrammer로 해당 ELF 또는 BIN을 0x08000000에 기록합니다. 자동 다운로드는 수행하지 않습니다.

HSI 16 MHz, Flash 512 KiB, RAM 128 KiB 기준입니다. PLL/FPU 설정은 사용하지 않습니다. ST 공식 GCC용 `firmware/vendor/startup_stm32f411xe.s`가 벡터 테이블과 data/bss 초기화를 담당합니다. IWDG는 약 1초(LSI 편차 있음)이며 슈퍼루프 완료마다 갱신합니다.

시작 순서는 `Reset_Handler → SystemInit → data 복사/bss 초기화 → __libc_init_array → main`입니다. `firmware/startup/system.c`는 프로젝트용 SystemInit(VTOR 설정, 리셋 기본 HSI 유지)과 Newlib의 `_init` 훅을 제공합니다. SystemInit은 RAM 초기화 전에 호출되므로 전역 변수에 접근하지 않습니다. 링커 스크립트는 런타임 초기화 배열을 보존합니다. SysTick/TIM2/EXTI ISR은 ST의 weak 핸들러를 기존 C 함수로 대체합니다. NMI/HardFault/MemManage/BusFault/UsageFault는 브리지 출력 억제 후 리셋하며, 나머지 미구현 인터럽트는 공식 Default_Handler의 무한 루프에 진입합니다(IWDG가 이미 시작됐다면 이후 watchdog 리셋).

## 계층과 파일

| 계층 | 파일 | 역할 |
|---|---|---|
| 실행 | `firmware/main.c`, `app/controller.c` | 초기화, 정지 우선 처리, ARM 상태, 보드별 App 실행 |
| L4 | `app/pick.c` | 하강 → 개폐/파지 확인 → 상승 |
| L4 | `app/axis.c` | X 이동 명령·원격 Z 상단 인터록 |
| L3 | `service/zlift.c`, `xmotion.c` | 도착/출발센서 이탈/시간 초과 검사 |
| L3 | `service/gripper.c`, `interlock.c` | 서보 단계 이동, 택트 스위치, 동작 허용 |
| L3 | `service/comm.c`, `status.c` | CRC·명령 번호·중복 처리·통신 lease·상태 |
| L2 | `driver/stepper.c`, `linact.c`, `servo.c` | STEP/DIR 펄스·방향/PWM·서보 펄스 |
| L2 | `driver/sensor.c`, `spislave.c`, `timer.c` | 디바운스, SPI2 DMA 프레임, ms 시간 기준 |
| L1 | `bsp/bsp.c` | 핀, 타이머, 클록, 인터럽트, ms tick |

L2 시간 API `timer_Get_Tick_Ms()`는 `timer.c`의 SysTick 카운터에서 제공합니다. 모든 선언은 `include/control.h`에 모았습니다. 보드별 설정은 `BOARD_ID` 컴파일 옵션으로 분리합니다. 미사용 함수는 링크 시 제거합니다.

## 기본 핀맵 — 기존 연결과 반드시 대조

두 보드는 같은 핀 번호를 사용해도 물리적으로 별도 보드입니다. PA2/PA3(ST-Link VCP)와 PA13/PA14(SWD)는 사용하지 않습니다.

| 신호 | STM32① Z/집게 | STM32② X |
|---|---|---|
| Z: L298N IN1 | PA0 | 미사용 (Low 유지) |
| Z: L298N IN2 | PA1 | 미사용 (Low 유지) |
| Z: L298N ENA | PB0 / TIM3 CH3 | 미사용 (Low 유지) |
| X: TB6600 PUL+ | — | PA8 / TIM1 CH1 (Nucleo D7) |
| X: TB6600 DIR+ | — | PA9 / GPIO (Nucleo D8) |
| X: TB6600 ENA+ | — | PC7 / GPIO (Nucleo D9, High=모터 해제, 미연결 가능) |
| TB6600 PUL− / DIR− / ENA− | — | STM32 GND |
| 미사용 (Low 유지) | PB1 | PA4, PA6, PB1 |
| 센서 0 | PC0: Z 상단 | PC0: A/Home |
| 센서 1 | PC1: Z 하단 | PC1: B/Load Port |
| 센서 2 | PC2: 집게 택트 스위치 | 미사용 |
| SG90 신호 | PB6 / TIM4 CH1 | 미사용 |
| 상태 LED | PA5 | PA5 |
| SPI2 NSS | PB12 | PB12 |
| SPI2 SCK | PB13 | PB13 |
| SPI2 MISO | PB14 | PB14 |
| SPI2 MOSI | PB15 | PB15 |

X축: TB6600 A+/A−에 한 코일, B+/B−에 다른 코일을 연결합니다. 실제 모터 선 색은 빨강·초록·노랑·파랑이며, 코일 쌍은 저항 측정으로 확인하십시오(같은 코일 수 Ω). TB6600 VCC/GND에 모터 전원(DC 9~42V)을 연결하고, 신호 입력은 옵토 절연이므로 모터 전원 GND와 STM32 GND를 연결할 필요가 없습니다. 펌웨어는 PA8(TIM1 CH1 PWM)에 STEP 펄스, PA9에 DIR, PC7에 ENA(High=해제)를 출력합니다. DIP 스위치 마이크로스텝(SW1~3)은 `CONFIG_X_MICROSTEP`(기본 2)과 일치시키고, 전류(SW4~6)는 모터 정격 이하로 설정합니다. TB6600 입력은 5V 기준이므로 3.3V 직결이 불안정하면 74HCT 버퍼를 사용합니다. Z축 2선 모터는 해당 보드 L298N OUT1/OUT2입니다.

ENA/ENB를 MCU로 제어할 채널은 모듈의 해당 enable 점퍼를 제거해야 합니다. 리셋 중 구동 방지를 위해 enable에 외부 풀다운을 사용하십시오. Z 모터 전원은 확인된 12V를 사용하고, SG90은 해당 서보 정격의 별도 안정화 전원으로 공급합니다. SG90에 12V를 연결하지 않습니다. 비절연 신호는 MCU·드라이버·서보 전원의 GND 기준을 공유합니다. 드라이버 모듈 5V 레귤레이터 점퍼는 모듈 사양에 따라 설정하며 Nucleo 전원과 임의로 병렬 연결하지 않습니다.

센서는 3.3V 입력 기준, 내부 풀업, LOW 감지입니다. 택트 스위치는 PC2–GND 연결을 가정합니다. 전압 출력형 포토센서는 출력 전압을 확인하고 필요 시 레벨 변환합니다. 포토인터럽터를 차단하는 플래그가 실제로 이동부와 함께 움직여야 합니다. SPI NSS에는 외부 풀업을 권장합니다.

## 동작과 튜닝

`firmware/include/config.h`에서 방향 반전, 속도, PWM, 시간 제한, 서보 펄스를 수정합니다. 핀 배치는 `bsp.c`와 SPI 핀 설정은 `spislave.c`에 있습니다.

- X축: 스텝 수 위치 제어(오픈루프). 기준 수치는 TEST.STEP_MOTOR_L298N(2분할, 400펄스/회전, 1000 Hz)과 같습니다. 500 Hz에서 출발해 약 250 ms 동안 1000 Hz까지 가속하고, 목표 펄스 전에 감속해 정확히 목표 펄스에서 정지합니다. `X_MOVE 0`은 위치 0(A), `X_MOVE 1`은 `CONFIG_X_TRAVEL_STEPS`(B)로 이동합니다. STOP 후에도 위치를 유지합니다. `CONFIG_X_HOME_MODE=0`(기본, 센서 없음)은 전원 인가 시 캐리지가 A에 있다고 가정하므로 손으로 A에 두고 켜야 합니다. `CONFIG_X_HOME_MODE=1`은 PC0 Home 센서를 쓰며, 기준을 잡기 전에는 `RECOVER`로 A 센서까지 저속 탐색합니다. 어느 모드에서든 A/B 센서가 감지되면 즉시 정지하고 위치를 0/TRAVEL로 보정하므로, 센서를 나중에 달아도 코드 변경이 필요 없습니다.
- Z축: 20 kHz PWM, 초기 듀티 70%. 상/하단 감지에서 정지. 상·하단 두 위치만 제어하므로 홈/Load Port 지지 높이를 맞춰야 합니다.
- 센서: 10 ms 안정 상태 후 반영. 펄스가 이보다 짧으면 감지하지 않으므로 플래그 폭과 이동 속도를 맞춰야 합니다.
- 이동 제한: 15초. 출발 센서가 1.5초 이상 계속 감지되면 오류. 두 끝 센서가 동시에 감지되면 오류.
- SG90: 20 ms 프레임, 열림 1100 us, 닫힘 1800 us 가정. 장착 방향과 실제 기구 한계에 맞춰 수정해야 합니다. 현재 구현은 닫힐수록 펄스가 증가하는 방향입니다. 닫기는 20 ms마다 5 us 증가하며 접촉이 확인되면 현재 목표 유지. 3초 내 미감지는 오류.
- 파지 전 열림 명령 및 스위치 해제 확인이 필요합니다. 열림 완료는 700 ms 경과+스위치 해제 조건이며 실제 각도 피드백은 없습니다.
- `PICK`: 하강 → 지지면에서 열림/스위치 해제 확인 → 닫기/접촉 → 상승. `PLACE`: 파지 상태 확인 → 하강 → 열림 → 상승.
- 파지 후 스위치가 해제되면 파지 상실 오류를 발생시킵니다. 택트 스위치는 접촉 확인이지 파지력 보증이 아닙니다.
- STOP/오류는 진행 명령을 취소하고 ARM을 해제합니다. 새 ARM 이후에도 이전 명령은 재개하지 않습니다. 새 명령 번호로 명시적으로 시작해야 합니다.
- X STOP 기본값은 코일 출력 해제입니다. 필요하면 온도/전류 확인 후 `CONFIG_X_HOLD_ON_STOP`을 변경합니다. Z STOP은 PWM=0으로 출력 비활성화하며 기계적 제동을 보장하지 않습니다.
- 집게 STOP은 추가 목표 갱신을 중단하고 마지막 서보 펄스를 유지합니다. SG90 자체가 마지막 목표로 이동하는 것을 즉시 멈추게 하는 명령은 아닙니다. 리셋 때는 서보 펄스도 사라집니다. 하중 유지에 필요한 기구 대책은 별도입니다.

## SPI 프로토콜 v2 — 24바이트

첨부한 24바이트 규약을 구현합니다. 기존 16바이트 v1 및 28바이트 제안과 호환되지 않습니다. 상세 필드 정의는 `jetson_STM32_v5_24바이트_제안.md`를 참조하십시오. 이 저장소에는 TSspi/TScontrol 소스가 없으므로 Jetson 태스크·큐·FSM 변경은 포함하지 않습니다. `tools/protocol.py`는 v2 참조 코덱입니다. `spi_test/`는 독립 배선 진단 프로젝트이며 모션 규약 검증 도구가 아닙니다.

| 대상 | 빌드 | wire axis | Jetson CS/장치 |
|---|---|---|---|
| Z·집게 STM32① | BOARD=1 | 2 | CS1, /dev/spidev0.1 |
| X STM32② | BOARD=2 | 1 | CS0, /dev/spidev0.0 |

SPI2 slave, mode 0, MSB first, 8 bit/word. 모든 다중 바이트 정수와 CRC는 **big endian**입니다. 초기 SCK 1 MHz는 실물 검증이 필요합니다. CS LOW 한 번에 정확히 24바이트, 프레임 사이 CS HIGH 최소 1 ms를 사용합니다. RX DMA1 Stream3/Channel0, TX DMA1 Stream4/Channel0이며 DMA/게시/완료 버퍼를 분리합니다. NSS 상승에서 길이·overrun·DMA 오류를 검사하여 TRANSPORT 고장으로 정지합니다.

요청과 동시에 받은 응답은 버리고 1 ms 후 NOP로 읽습니다. DMA TX 버퍼가 앞선 CS 상승에서 준비되므로 첫 NOP에는 이전 응답이 나올 수 있습니다. 작업을 재전송하지 말고 500 ms 안에서 NOP로 ack_seq를 확인하십시오. 축별 100 ms 조회, 모든 요청에 최신 remote를 사용합니다.

| 요청 오프셋 | 내용 |
|---|---|
| 0..3 | A5, version=2, axis, cmd |
| 4..7 | seq uint16, arg, remote |
| 8..11 | target int32, 현재 0 |
| 12..21 | option/reserved, 모두 0 |
| 22..23 | CRC-16/CCITT-FALSE, 바이트 0..21 대상 |

| 응답 오프셋 | 내용 |
|---|---|
| 0..3 | 5A, version=2, axis, state |
| 4..7 | ack_seq uint16, receipt, fault |
| 8..9 | sensors, relay(bit0..2)/stage(bit3..6)/link(bit7) |
| 10..11 | op_seq uint16 |
| 12..16 | position int32 (‰), pos_src |
| 17 | reserved=0 |
| 18..19 | heartbeat uint16, 메인 루프에서 100 ms마다 증가 |
| 20..21 | reserved=0 |
| 22..23 | CRC |

state: UNKNOWN=0, IDLE=1, MOVING=2, COMPLETE=3, FAULT=4. 내부 C 이름 STOPPED/BUSY/DONE/ERROR도 이 값을 사용합니다. 오류: OK=0, STOP=1, LINK=2, SENSOR=3, TIMEOUT=4, INTERLOCK=5, GRIP=6, BUSY=7, COMMAND=8, SEQUENCE=9, TRANSPORT=10. BUSY/COMMAND/SEQUENCE는 receipt에만 기록합니다.

외부 명령: NOP=0, ARM=1, STOP=2, RECOVER=3, X_MOVE=16(arg=0/1), PICK=36, PLACE=37. 기존 Z_UP/Z_DOWN/OPEN/CLOSE는 내부 코드일 뿐 SPI에서 수락하지 않습니다. 잘못된 cmd/arg/target은 COMMAND로 거부합니다. header/axis/CRC/option/reserved 오류는 폐기하고 링크를 갱신하지 않습니다. NOP는 seq/arg/target=0만 유효합니다.

STOP은 seq·MOVING 검사를 우회하여 정지하고 ARM을 해제합니다. 잘못된 STOP arg/target도 출력은 정지시키되 receipt=COMMAND를 보고합니다. 정상 STOP은 UNKNOWN/fault=STOP, 기존 FAULT에서는 최초 fault와 stage를 유지합니다. ARM 성공만 고장을 해제합니다. 부팅은 UNKNOWN/fault=0/stage=BOOT(15), ARM 성공 후 IDLE/stage=NONE입니다.

새 seq는 16비트 차이 1..32767이며 65535 다음은 1입니다. 동일 seq/cmd/arg/target은 재실행하지 않습니다. 거부된 새 요청도 번호를 소비하고 STOP도 이력을 갱신합니다. op_seq는 수락한 작업에만 기록하고 ARM/STOP/NOP에는 유지합니다. 완료는 센서 확인 후에만 보고하며 다음 작업까지 유지합니다.

X 이동/RECOVER는 remote bit0(Z 상단), Z RECOVER는 bit1(X 정지), PICK/PLACE는 bit1+bit2(X 유효 정거장)가 필요합니다. 동작 중 조건 상실 시 INTERLOCK 정지합니다. Z RECOVER는 집게를 열지 않습니다. Z relay bit0은 상단 센서 및 비이동, X bit1은 비이동, bit2는 유효 끝 센서를 나타냅니다.

Jetson은 X 요청 remote=최신 Z relay & 1, Z 요청 remote=최신 X relay & 6을 사용하고 상대 응답이 300 ms보다 오래되면 0을 넣어야 합니다. ARM 중 유효 요청이 500 ms 없으면 LINK 정지합니다. NOP 횟수는 heartbeat에 영향을 주지 않습니다.

위치 표시(0..1000)는 진단용입니다. 단, X축 이동 완료는 스텝 카운터(펄스 수)로 판정합니다. 끝 센서는 pos_src=2, 0/1000; 이후 X 스텝/Z 시간 추정은 pos_src=1, 1..999; 끝 센서 미확인/모순은 pos_src=0입니다. `CONFIG_X_TRAVEL_STEPS`(기본 10회전=4000펄스, 리드 mm/회전으로 실측 보정), `CONFIG_Z_TRAVEL_MS=10000`은 **미보정 초기값**이므로 실측값으로 수정하십시오. Z축 완료와 인터락에는 위치를 사용하지 않습니다.

CRC C 참조: `firmware/service/comm.c`의 `comm_Get_Crc()`, 123456789 → 0x29B1. 고정 시험 벡터: `tests/protocol_vectors.json`.

```sh
python tools/protocol.py 2 arm --seq 1
python tools/protocol.py 2 recover --seq 2 --remote 2
python tools/protocol.py 1 x_move --seq 2 --arg 1 --remote 1
python tools/protocol.py 1 nop
```

## 시작 순서와 검증 범위

1. 핀맵, 입력 극성, 모터 방향, 서보 기구 범위를 실제 배선과 맞춥니다.
2. 부팅 후 25 ms 이상 기다리고 두 보드에 NOP를 시작합니다. 부팅 직후에는 모터/서보 출력이 비활성입니다.
3. 실제 상태로 원격 비트를 갱신하면서 양축 NOP로 ack_seq 동기화 → STOP → ARM → Z RECOVER 완료 → X RECOVER 완료 순서로 기준 위치를 잡습니다. Z가 움직이는 동안 X 이동 명령을 보내지 않습니다.
4. 홈에서 PICK 완료와 Z 상단을 확인한 뒤 X_MOVE(B), 도착 후 PLACE를 실행합니다.
5. 구동 중 STOP, 통신 중단, 센서 미검출, 스위치 미접촉을 시험합니다. 실제 SPI 프레임 경계·타이머 파형·정지 거리·하중 유지도 실측해야 합니다.

호스트 테스트는 실제 app/service/driver 코드를 링크하고 GPIO·시간·SPI transport만 대체합니다. 센서 도착, STOP/ARM, 통신 단절, 인터록 상실, 센서 모순, 이동/출발 이탈 타임아웃, 중복 명령, CRC, BUSY 거부, transport 오류, tick wrap, 디바운스, PICK/PLACE, 파지 실패·상실을 확인합니다. DMA/NVIC/전기적 특성은 이 테스트로 검증되지 않습니다.

## 코딩 규칙과 외부 코드

작성 코드의 일반 함수는 `파일명_Init/Get/Set_기능` 형식입니다. `main`, CMSIS ISR/Reset 벡터 이름, `SystemInit`, `SystemCoreClock`, Newlib `_init`은 연동상 예외입니다. 매크로는 대문자, 파일 전역 변수는 파일명 접두어를 사용합니다. MISRA를 고려한 고정 크기 버퍼/명시적 상태/시간 wrap 처리/동적 할당 배제 구조이나 **MISRA 정적 분석과 정식 준수 판정은 수행하지 않았습니다.** 하드웨어 주소/포인터 변환과 외부 CMSIS·어셈블리 코드 등은 별도 deviation 검토 대상입니다.

공식 CMSIS 헤더와 수정하지 않은 ST GCC 시작 파일을 `firmware/vendor`에 포함했습니다. ST cmsis-device-f4 v2.6.10과 ARM CMSIS_5 5.9.0을 사용하며 각각의 라이선스를 동봉했습니다. 재다운로드는 `python tools/fetch_cmsis.py`입니다.

참조:
- ST STM32F411 RM0383: https://www.st.com/resource/en/reference_manual/dm00119316.pdf
- ST L298 데이터시트: https://www.st.com/resource/en/datasheet/l298.pdf
- ST CMSIS: https://github.com/STMicroelectronics/cmsis-device-f4/tree/v2.6.10
- ARM CMSIS: https://github.com/ARM-software/CMSIS_5/tree/5.9.0
