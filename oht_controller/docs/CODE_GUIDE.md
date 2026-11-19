# TScontrol / TSspi v2 코드 읽는 순서

## 중앙 FSM

[TScontrol.c](../app/TScontrol.c)는 이벤트 종류를 확인하고 다음 상태의 명령을 보내는 구조다.

1. `TScontrol_Init_Task` → 입력 큐에서 이벤트 하나 읽기 → `TScontrol_Parse`.
2. `TScontrol_Parse_Command` → START/STOP/RECOVER 판정.
3. `TScontrol_Parse_Axis` / `TScontrol_Parse_E84` → 확인된 완료에 따라 다음 상태 선택.
4. `TScontrol_Set_Transition` → 그 상태에 필요한 SPI 명령 또는 E84 요청 보내기.

초기화·명시적 복구는 STOP 양축 → ARM Z/X → RECOVER Z/X → IDLE이다. 정지 확인은 `STOPPED`, 제어 허용 확인은 `ARMED`, 실제 작업 완료는 `COMPLETE` 이벤트를 사용한다.

반송은 PICK → X_MOVE → E84 준비 → PLACE → E84 완료 → COMPLETE이다. `TScontrol_move`에 기다리는 축과 작업 종류를 보관하며 위치값은 비교하지 않는다. 인계는 PLACE 완료와 L_REQ OFF가 모두 와야 진행한다. PLACE에 상승이 포함되어 별도 Z 복귀는 없다.

고장 시 양축 STOP을 요청한다. START는 고장을 해제하지 않으며 RECOVER 명령으로 복구한다. TCP 통보는 알림 큐에 보관하고 TCP 태스크가 송신한다. 입력 큐나 알림 큐가 넘쳐도 고장과 STOP으로 처리한다.

## SPI

[TSspi.c](../app/TSspi.c)는 다음 순서로 읽는다.

1. `TSspi_Init_Task` → STOP 요청 반영 → X/Z NOP 조회 → 일반 명령 하나 처리.
2. `TSspi_Parse_Command` → 명령 종류·대상·ARM·큐 차단 여부를 확인하고 `TSspi_Set_Start`.
3. `TSspi_Set_Exchange` → 요청 24바이트 → 1ms → NOP 24바이트.
4. `TSspi_Parse` → 상태/Heartbeat 갱신 → `TSspi_Set_Receipt`.
5. `TSspi_Set_Receipt` → ack_seq로 접수, op_seq로 실제 작업 완료 확인.
6. `TSspi_Check_Timeout` → 응답·Heartbeat·접수·작업 제한시간 확인.

| 대기 단계 | 의미 |
|---|---|
| TSspi_PENDING_NONE | 대기 없음 |
| TSspi_PENDING_RECEIPT | 요청 전송 후 ack_seq/receipt 확인 중 |
| TSspi_PENDING_COMPLETE | 접수되었으며 op_seq/COMPLETE 확인 중 |

일반 명령을 반복 송신하지 않고 NOP로 조회한다. STOP은 일반 큐와 별도 비트로 등록하여 큐가 가득 차도 우선 처리한다. STOP만 같은 seq로 반복한다. 구조체·enum·설정은 [TSspi.h](../app/TSspi.h), [TScontrol.h](../app/TScontrol.h), 공통 자료형은 [main.h](../app/main.h)에 있다.

프레임 오프셋·CRC·초기 실제 복구 이동·E84 미구현 범위·시험 방법은 [SPI 규약](SPI_PROTOCOL.md)을 따른다. 시퀀스는 [Mermaid 문서](SEQUENCE_DIAGRAMS.md)에 있다.
