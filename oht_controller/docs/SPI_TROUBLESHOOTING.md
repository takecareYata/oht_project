# TSspi 트러블슈팅

Jetson Orin Nano의 40핀 헤더와 X/Z STM32의 SPI v2 연결을 기준으로 한다. `START`가 거부되면 GUI의 `SET_STATUS(0x0013)`와 `SET_ALARM(0xFFFF)`에서 중앙 상태를 확인하고, 같은 구간의 Jetson stderr에서 축별 `TSspi` 로그를 찾는다. 프레임 구조와 상태 코드 전체는 [SPI_PROTOCOL.md](./SPI_PROTOCOL.md)에 있다.

## 양축 응답이 전부 `00`이었던 사례

TCP는 `msgid=0x0010, payload=1`을 정상 수신했다. SPI에서는 X/Z 모두 장치를 열었지만 24바이트 전체를 `00`으로 읽었다. 이후 초기 STOP 확인에 실패해 중앙 상태가 `STOPPING(10)`에서 `FAULT(9), fault=2`로 바뀌었고, START 응답은 `SET_COMMAND(0x0011), payload=2(REJECTED)`였다.

| 로그 또는 값 | 의미 |
| --- | --- |
| `device opened detail=0x000F4240` | 장치 열기와 SPI 설정 호출이 완료됐다. `detail`은 설정한 속도 1,000,000 Hz다. STM32 응답은 이후 프레임에서 확인한다. |
| `RX CRC mismatch`, `detail=0x00009FB4`, `bytes=00 … 00` | 앞 22바이트가 모두 0일 때 계산한 CRC가 `0x9FB4`다. 수신 CRC는 `0x0000`이며 정상 응답 헤더 `5A 02`도 없다. |
| `ack=0`, `hb=0` | 응답 검증에 실패해 Jetson 상태 캐시가 초기값에 머물러 있다. Heartbeat는 유효 프레임을 확보한 뒤 조사한다. |
| `command start cmd=0x00000002`, 이어서 `FAULT -> priority STOP` | Jetson이 SPI STOP을 시도한 뒤 고장을 처리했다. 실제 정지 확인 기준은 축별 `STOP confirmed` 로그다. |

이 사례에서는 Jetson-IO의 활성 목록에 `spi1`이 빠져 있었다. 양축이 동시에 같은 증상을 보이면 40핀 헤더의 SPI 핀 설정과 공용 SCLK·MOSI·MISO·GND부터 확인한다. Linux의 SPI 전송은 상대 장치의 응답 없이도 정상 길이를 반환할 수 있다. [TSspi.c](../app/TSspi.c) · [Linux SPI 사용자 공간 API](https://docs.kernel.org/6.8/spi/spidev.html)

## 장치와 Jetson 핀 설정 확인

1. 장치 경로와 현재 SPI 컨트롤러 연결을 확인한다. 이 보드에서는 두 장치 모두 `3210000.spi` 아래에 있고, CS0은 X축, CS1은 Z축이다.

   ```sh
   ls -l /dev/spidev0.0 /dev/spidev0.1
   readlink -f /sys/class/spidev/spidev0.0 /sys/class/spidev/spidev0.1
   ```

2. Jetson 터미널에서 **Header 1**의 활성 목록에 `spi1 (19,21,23,24,26)`이 있는지 확인한다. 이 보드에서 Jetson-IO의 이름은 `spi1`이고, 애플리케이션은 Linux 장치 번호 `0.0`, `0.1`을 사용한다.

   ```sh
   sudo /opt/nvidia/jetson-io/config-by-function.py -l enabled
   ```

3. 목록에 `spi1`이 없으면 아래 명령으로 다음 부팅에 사용할 헤더 설정을 저장한다. 장비를 중단할 수 있는 시점에 재부팅하고 2번 명령으로 적용 여부를 확인한다. 지원 기능 목록은 같은 도구의 `-l all`로 확인할 수 있다. [NVIDIA Jetson-IO 문서](https://docs.nvidia.com/jetson/archives/r36.4.4/DeveloperGuide/HR/ConfiguringTheJetsonExpansionHeaders.html#config-by-function-configure-header-s-by-special-function)

   ```sh
   sudo /opt/nvidia/jetson-io/config-by-function.py -o dt spi1
   ```

## 배선과 파형 확인

| 신호 | Jetson 40핀 헤더의 물리 번호 | STM32 SPI2 |
| --- | --- | --- |
| MOSI | 19 | 양축 PB15 |
| MISO | 21 | 양축 PB14 |
| SCLK | 23 | 양축 PB13 |
| X축 CS0 | 24 | X 보드 PB12 (NSS) |
| Z축 CS1 | 26 | Z 보드 PB12 (NSS) |
| GND | 25 등 | 양축 GND |

핀 설정 적용 후에도 `00`만 들어오면 다음 순서로 신호를 확인한다.

1. 전송 중 Jetson 23번에서 SCLK가 움직이고, 24번 또는 26번 CS가 해당 축을 조회할 때 LOW가 되는지 본다. 기본 설정은 Mode 0, MSB first, 8비트, 1 MHz다. CS 한 구간에 24바이트(192클록)를 보내고, 다음 프레임 전에 CS HIGH를 최소 1 ms 유지한다. [TSspi.h](../app/TSspi.h)
2. 같은 SCLK/CS가 선택된 STM32의 PB13/PB12에 도달하는지 확인한다. Jetson에서만 파형이 보이면 배선과 공통 GND를 점검한다. STM32까지 도달하는데 PB14가 계속 LOW이면 보드의 전원·리셋·SPI2 설정과 TX DMA를 확인한다. STM32의 `spislave_diag.cs_count`, `rx_remaining`, `invalid_count`로 NSS 에지와 프레임 수신 상태를 구분할 수 있다. [STM32 SPI2 드라이버](https://github.com/takecareYata/oht_project/blob/68f4e76ce9b5ab6eb11f4666a4ed9b4c738639a7/stm32_x/firmware/driver/spislave.c)
3. PB14에서 응답 파형이 보이면 Jetson 21번까지 같은 신호가 도달하는지 확인한다. MISO를 공유하므로 선택되지 않은 STM32가 이 선을 구동하지 않도록 구성해야 한다.
4. NOP로 읽은 정상 응답의 첫 3바이트는 X축 `5A 02 01`, Z축 `5A 02 02`다. 축 번호가 반대로 나오면 CS 배선과 펌웨어 빌드를 확인한다. 공유한 STM32 펌웨어는 **X축 `BOARD=2`**, **Z축 `BOARD=1`**이며, `make`의 기본값은 `BOARD=1`이다. [STM32 빌드 설정](https://github.com/takecareYata/oht_project/blob/68f4e76ce9b5ab6eb11f4666a4ed9b4c738639a7/stm32_x/Makefile) · [STM32 축 설정](https://github.com/takecareYata/oht_project/blob/68f4e76ce9b5ab6eb11f4666a4ed9b4c738639a7/stm32_x/firmware/include/config.h)

## 나머지 로그별 확인 지점

| 로그 | 먼저 확인할 항목 |
| --- | --- |
| `open failed` | `detail`은 errno의 16진수 표현이다. `0x02`이면 장치 경로와 등록 상태, `0x0D`이면 실행 사용자의 접근 권한을 확인한다. |
| `SPI_IOC_WR_* failed`, `SPI transfer failed`, `SPI short transfer` | 실패한 ioctl과 errno를 확인한다. 짧은 전송의 `detail`은 반환된 바이트 수이며 기대값은 24다. |
| `RX CRC mismatch`와 0이 아닌 RX 데이터 | 수신 마지막 2바이트를 기대 CRC와 비교한다. 24바이트 경계, Mode 0, 프레임 간 CS HIGH 시간, CRC-16/CCITT-FALSE와 다중 바이트 필드 순서를 점검한다. |
| `RX header/fields invalid`, `RX axis mismatch` | 헤더 `5A 02`, X=1/Z=2, 예약 필드와 값의 범위를 확인한다. |
| `valid response timeout` | 기본 500 ms 동안 유효 응답이 없다. 직전 전송 실패나 프레임 검증 로그부터 확인한다. |
| `heartbeat timeout`, `MCU BOOT after ARM` | 유효 응답의 Heartbeat가 기본 1000 ms 동안 바뀌지 않았거나 ARM 이후 BOOT stage가 감지됐다. STM32 메인 루프, 재부팅 및 전원 상태를 확인한다. |
| `receipt timeout`, `MCU receipt rejected` | 기본 500 ms 내에 해당 요청의 접수가 확인되는지 `seq`와 `ack`를 비교한다. STOP과 ARM은 해당 상태 조건도 만족해야 한다. 거부 시 SPI 응답의 `receipt` 및 `fault` 값을 확인한다. |
| `work timeout` | 작업 완료 응답의 `op_seq`, COMPLETE 상태, MCU fault를 확인한다. 기본 제한은 RECOVER/X_MOVE 30초, PICK/PLACE 45초다. |

GUI의 중앙 `fault=2`는 SPI 통신 고장을 뜻한다. STM32 응답의 `receipt`와 `fault`는 별도 코드 체계를 사용한다. [SPI 오류 코드](./SPI_PROTOCOL.md#상태-전달-및-제한시간) · [중앙 상태 및 오류 정의](../app/main.h)

## 복구와 재시험

**제어 프로그램이 시작되거나 RECOVER가 수락되면 축이 실제로 움직일 수 있다.** 구동 상태를 확인한 뒤 재시험한다.

1. 양축 `seq synchronized`와 `heartbeat normal`을 확인한다. 초기 시퀀스는 양축 STOP → ARM Z/X → RECOVER Z/X → IDLE이다. 로그로는 양축의 `STOP confirmed`, `ARM confirmed`, RECOVER의 `work complete`를 확인할 수 있다.
2. 연결을 고친 뒤에도 중앙 FAULT가 유지되면 TCP `GET_COMMAND(0x0010), payload=3(RECOVER)`로 복구를 요청한다. 고장 중 SPI 장치 재열기는 기본 1초 간격으로 이뤄지며, 재열기 이후에는 감시와 STOP을 시도한다.
3. GUI에 `state=1(IDLE), fault=0`이 표시되면 START를 보내고 `SET_COMMAND(0x0011), payload=1(ACCEPTED)`을 확인한다.

기본 설정에서 오류 RX의 24바이트 HEX가 stderr에 남는다. 전체 TX/RX가 필요한 경우에만 프로젝트 루트에서 아래 옵션으로 다시 빌드한다. 로그는 비동기 출력이므로 축과 요청 번호로 대응시킨다. [디버그 설정](./SPI_PROTOCOL.md#spi-디버그-출력)

```sh
make -B -C app CPPFLAGS="-DTSspi_DEBUG_FRAMES_ENABLED=1"
```
