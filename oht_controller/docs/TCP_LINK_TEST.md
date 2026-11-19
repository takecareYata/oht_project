# TCP LinkTest 서버 테스터

Python 표준 라이브러리만 사용한다. `oht_controller` 폴더에서 실행한다.

먼저 터미널 1에서 서버를 실행한다.

```sh
python3 test/tcp_link_test_server.py
```

`서버 대기` 메시지가 나오면 터미널 2에서 C 애플리케이션을 빌드하고 실행한다.
기본 연결 대기시간은 30초이다. C 클라이언트는 서버가 없거나 연결이 종료되면
1초 대기 후 재접속하므로 어느 쪽을 먼저 실행해도 된다.

```sh
make -C app
./app/build/oht_controller
```

테스터가 클라이언트 연결을 받으면 `msgid=0x0000`인 LinkTest 요청을 보내고,
14바이트 응답을 조립해 원시 바이트와 각 필드를 출력한다. C 코드에서는
`TStcp_Parse()` → `TStcp_LinkTest()`를 통해 응답한다. 성공 시 `PASS`를 출력하고
종료 코드 0으로 종료한다. 연결·수신 실패나 예상 응답 불일치는 종료 코드 1이다.
한 번 실행할 때 한 클라이언트와 한 번의 요청/응답을 처리한 뒤 소켓을 닫는다.
C 애플리케이션은 계속 실행되며, 테스터를 다시 실행하면 다시 연결해 테스트한다.
C 애플리케이션을 종료하려면 실행 중인 터미널에서 `Ctrl+C`를 누른다.

| 필드 | 크기 | 기본 요청 | 예상 응답 |
|---|---:|---:|---:|
| msgid | 2바이트 | 0x0000 | 0x0000 |
| len | 4바이트 | 4 | 4 |
| payload | 4바이트 | 0 | 0x00001234 |
| reserved | 2바이트 | 0 | 0 |
| crc | 2바이트 | 0x0004 | 0x004A |

전송 순서는 빅 엔디언이며 Python 형식은 `!HIIHH`이다. C 구조체의 패딩은
전송하지 않는다. CRC는 마지막 crc 필드를 제외한 12바이트를 한 바이트씩 더한
합계의 하위 16비트이다. 테스터는 요청 CRC를 계산하고 응답 CRC도 같은 규칙으로
확인한다. C 클라이언트의 수신 CRC 거부 처리는 아직 추가하지 않았다.

요청 payload와 대기시간은 변경할 수 있다.

```sh
python3 test/tcp_link_test_server.py --payload 0xAABBCCDD --timeout 60
```

Python 서버 기본값은 `127.0.0.1:5000`이며, 현재 C 클라이언트의 기본 목적지는
`10.10.15.32:5000`이다. 로컬 시험 시 두 주소를 일치시켜야 한다. 서버의 `--host`·`--port`를
변경하면 C 클라이언트의 `TStcp_SERVER_ADDRESS`·`TStcp_SERVER_PORT`도 같은 목적지로
맞춰 다시 빌드해야 한다.

## 전용 TCP STOP 요청

[수신 처리기](../app/TStcp.c)의 `TStcp_Get_Stop`은 GET_STOP(msgid=`0x0020`), len=4 요청을 중앙 FSM의 STOP 명령으로 전달한다. payload는 사용하지 않으며 송신 측에서는 0을 권장한다. NULL, 다른 msgid, len≠4는 INVALID_ARGUMENT로 처리한다. 수신 CRC 검증 정책은 기존과 동일하다.

payload=0, reserved=0 요청의 14바이트 HEX는 다음과 같다. 마지막 2바이트는 앞 12바이트 합산이다.

```text
00 20 00 00 00 04 00 00 00 00 00 00 00 24
```

호출 흐름은 `TStcp_Parse → TStcp_Get_Stop → TScontrol_Set_Command(STOP) → TScontrol_Set_Fault_Internal → TScontrol_Set_Stop → TSspi_Set_Axis_Command(STOP)`이다. SPI v2의 일반 명령 큐를 지우고 양축 STOP을 우선 처리한다. 실제 송수신은 SPI 태스크가 담당한다.

요청 접수 결과는 기존 SET_COMMAND(`0x0011`, ACCEPTED=1) 알림이며, 중앙 FSM은 FAULT_STOP=5를 유지한다(기존 FAULT이면 최초 원인 유지). SET_STATUS/SET_ALARM도 기존 알림 큐로 전송한다. SET_STOP(`0x0021`) 응답은 현재 구현하지 않는다. 접수 알림은 실제 정지 확인을 뜻하지 않으며, 실제 확인에는 STM32의 해당 STOP ack_seq·receipt=0·UNKNOWN/FAULT 응답이 필요하다. 재가동은 명시적 RECOVER 명령을 사용한다.

`make -C app test`는 [TCP STOP 파서 시험](../test/test_tcp_stop.c)과 [SPI v2/중앙 FSM 모의 시험](../test/test_tsspi.c)을 실행한다. 실물 장치와 네트워크에 명령을 보내지 않는다.
