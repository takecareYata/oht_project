# TCP / 중앙 FSM / SPI v2 시퀀스

현재 [TStcp.c](../app/TStcp.c), [TScontrol.c](../app/TScontrol.c), [TSspi.c](../app/TSspi.c)의 함수명을 표시한다. 상세 값은 [SPI v2 규약](SPI_PROTOCOL.md)을 따른다. E84는 요청·이벤트 인터페이스만 있으며 [TSe84.c](../app/TSe84.c)의 GPIO 동작은 아직 구현되지 않았다.

## TCP 수신과 알림 큐

```mermaid
sequenceDiagram
    participant GUI as TCP 서버
    participant TCP as TStcp 태스크
    participant CONTROL as TScontrol 태스크
    TCP->>TCP: TStcp_Init_Task()
    TCP->>GUI: TStcp_Init_Communication() / connect
    GUI->>TCP: 14바이트 요청 (GET_COMMAND)
    TCP->>TCP: TStcp_Init_Client() / TStcp_Get_Frame()
    TCP->>TCP: TStcp_Parse() → TStcp_Get_Command()
    TCP->>CONTROL: TScontrol_Set_Command() (입력 큐 복사)
    CONTROL->>CONTROL: TScontrol_Get_Event() → TScontrol_Parse()
    CONTROL->>CONTROL: TScontrol_Parse_Command()
    CONTROL->>CONTROL: TScontrol_Set_Notice() (알림 큐 복사)
    TCP->>TCP: TScontrol_Process_Notifications()
    TCP->>TCP: TStcp_Set_Command_Result() / TStcp_Set_Status()
    TCP->>GUI: TStcp_Send() 또는 TStcp_Set_Send_Bytes()
    Note over TCP,CONTROL: SET_STATUS는 30바이트, 일반 알림은 14바이트
    GUI->>TCP: LINK_TEST (msgid=0)
    TCP->>TCP: TStcp_Parse() → TStcp_LinkTest()
    TCP->>TCP: TStcp_Set_LinkTest() → TStcp_Send()
    TCP->>GUI: payload=0x1234 응답
```

## 프로세스 시작 / 명시적 복구

처음 실행할 때 아래 복구 이동이 발생할 수 있다. 고장 후에는 GUI의 RECOVER=3 명령이 있어야 이 절차를 시작한다. START=1은 복구 명령이 아니다.

```mermaid
sequenceDiagram
    participant CONTROL as TScontrol
    participant SPI as TSspi
    participant X as STM32 X
    participant Z as STM32 Z
    CONTROL->>CONTROL: TScontrol_Init_Task() → TScontrol_Init_Fsm()
    CONTROL->>CONTROL: TScontrol_Parse() → TScontrol_Set_Recovery()
    CONTROL->>SPI: TSspi_Set_Recovery() (일반 큐 삭제, STOP 우선 등록)
    CONTROL->>CONTROL: TScontrol_Set_Transition(STOPPING)
    SPI->>SPI: TSspi_Init_Communication() / TSspi_Set_Open()
    SPI->>SPI: TSspi_Set_Stop_Requests()
    SPI->>X: TSspi_Set_Poll() → TSspi_Set_Exchange(NOP)
    X-->>SPI: ack_seq 동기화
    SPI->>Z: TSspi_Set_Poll() → TSspi_Set_Exchange(NOP)
    Z-->>SPI: ack_seq 동기화
    SPI->>X: TSspi_Set_Start(STOP) / TSspi_Set_Exchange()
    SPI->>Z: TSspi_Set_Start(STOP) / TSspi_Set_Exchange()
    Note over SPI,Z: 모든 교환은 요청 → 1ms 대기 → NOP 응답 읽기
    X-->>SPI: ack_seq 일치, receipt=0, UNKNOWN 또는 FAULT
    Z-->>SPI: ack_seq 일치, receipt=0, UNKNOWN 또는 FAULT
    SPI->>CONTROL: TSspi_Set_Receipt() → TScontrol_Set_Axis_Event(STOPPED), 양축
    CONTROL->>CONTROL: TScontrol_Parse_Axis() → TScontrol_Set_Transition(ARMING_Z)
    CONTROL->>SPI: TScontrol_Set_Axis_Command() → TSspi_Set_Axis_Command(ARM Z)
    SPI->>Z: TSspi_Parse_Command() → TSspi_Set_Start(ARM)
    Z-->>SPI: ack_seq 일치, receipt=0, IDLE, fault=0
    SPI->>CONTROL: TScontrol_Set_Axis_Event(ARMED Z)
    CONTROL->>SPI: TScontrol_Set_Transition(ARMING_X) → TSspi_Set_Axis_Command(ARM X)
    SPI->>X: TSspi_Parse_Command() → TSspi_Set_Start(ARM)
    X-->>SPI: ARM 확인
    SPI->>CONTROL: TScontrol_Set_Axis_Event(ARMED X)
    CONTROL->>SPI: TScontrol_Set_Transition(RECOVER_Z) → TSspi_Set_Axis_Command(RECOVER Z)
    SPI->>Z: TSspi_Parse_Command() → TSspi_Set_Start(RECOVER)
    loop Z 복구 완료까지 NOP
        SPI->>Z: TSspi_Set_Poll() → TSspi_Set_Exchange(NOP)
        Z-->>SPI: ack_seq / op_seq / state / fault
        SPI->>SPI: TSspi_Parse() → TSspi_Set_Receipt()
    end
    SPI->>CONTROL: TScontrol_Set_Axis_Event(COMPLETE Z)
    CONTROL->>SPI: TScontrol_Parse_Axis() → TScontrol_Set_Transition(RECOVER_X)
    SPI->>X: TSspi_Parse_Command() → TSspi_Set_Start(RECOVER)
    X-->>SPI: op_seq 일치, COMPLETE, fault=0
    SPI->>CONTROL: TScontrol_Set_Axis_Event(COMPLETE X)
    CONTROL->>CONTROL: TScontrol_Parse_Axis() → TScontrol_Set_Transition(IDLE)
    Note over CONTROL,SPI: 고장은 양축 복구 완료 후 해제. 작업은 자동 재개하지 않음
```

## 중앙 FSM의 정상 반송

```mermaid
sequenceDiagram
    participant TCP as TStcp
    participant CONTROL as TScontrol
    participant SPI as TSspi
    participant E84 as TSe84 (구현 예정)
    TCP->>CONTROL: TScontrol_Set_Command(START)
    CONTROL->>CONTROL: TScontrol_Parse_Command() → TScontrol_Is_Ready()
    alt 양축 ARM·Heartbeat·E84 인터록 정상
        CONTROL->>CONTROL: TScontrol_Set_Transition(WAFER_PICKUP)
        CONTROL->>SPI: TScontrol_Set_Axis_Command(Z, PICK) → TSspi_Set_Axis_Command()
        Note over SPI: STM32가 하강/집게/상승을 실행하는 동안 NOP 조회
        SPI-->>CONTROL: TScontrol_Set_Axis_Event(COMPLETE, PICK)
        CONTROL->>CONTROL: TScontrol_Parse_Axis() → TScontrol_Set_Transition(X_TRANSFER)
        CONTROL->>SPI: TSspi_Set_Axis_Command(X, X_MOVE, arg=1)
        SPI-->>CONTROL: TScontrol_Set_Axis_Event(COMPLETE, X_MOVE)
        CONTROL->>CONTROL: TScontrol_Parse_Axis() → TScontrol_Set_Transition(E84_PREPARE)
        CONTROL->>E84: TSe84_Set_Request(PREPARE)
        E84-->>CONTROL: TScontrol_Set_E84_Event(READY)
        CONTROL->>CONTROL: TScontrol_Parse_E84() → TScontrol_Set_Transition(WAFER_HANDOVER)
        CONTROL->>E84: TSe84_Set_Request(BUSY)
        CONTROL->>SPI: TSspi_Set_Axis_Command(Z, PLACE)
        par 도착 순서는 무관
            SPI-->>CONTROL: TScontrol_Set_Axis_Event(COMPLETE, PLACE)
            CONTROL->>CONTROL: TScontrol_Parse_Axis() → TScontrol_Set_Handover_Complete()
        and
            E84-->>CONTROL: TScontrol_Set_E84_Event(LOAD_REQUEST_OFF)
            CONTROL->>CONTROL: TScontrol_Parse_E84() → TScontrol_Set_Handover_Complete()
        end
        CONTROL->>CONTROL: TScontrol_Set_Transition(E84_COMPLETE)
        CONTROL->>E84: TSe84_Set_Request(COMPLETE)
        E84-->>CONTROL: TScontrol_Set_E84_Event(COMPLETE)
        CONTROL->>CONTROL: TScontrol_Parse_E84() → TScontrol_Set_Transition(COMPLETE)
        Note over CONTROL,SPI: PLACE에 상승 포함. 별도 Z_RETURN 없음
        CONTROL->>CONTROL: TScontrol_Set_Notice(STATUS / COMPLETE)
        TCP->>TCP: TScontrol_Process_Notifications() → TStcp_Set_Status() / TStcp_Set_Complete()
    else 인터록 불만족
        CONTROL->>CONTROL: TScontrol_Set_Notice(COMMAND, REJECTED)
    end
```

## SPI 접수·실제 완료·고장

```mermaid
sequenceDiagram
    participant CONTROL as TScontrol
    participant SPI as TSspi
    participant MCU as 선택한 STM32
    CONTROL->>SPI: TSspi_Set_Axis_Command() (큐)
    SPI->>SPI: TSspi_Get_Command() → TSspi_Parse_Command()
    SPI->>SPI: TSspi_Set_Start() (새 seq, 접수 대기)
    SPI->>MCU: TSspi_Set_Transfer(요청 24바이트)
    Note over SPI,MCU: CS 해제 / TSspi_Set_Delay(최소 1ms)
    SPI->>MCU: TSspi_Set_Transfer(NOP 24바이트)
    MCU-->>SPI: 응답 24바이트
    SPI->>SPI: TSspi_Get_Response() → TSspi_Parse() → TSspi_Set_Receipt()
    loop ack_seq 반영 지연 시 500ms 이내 NOP
        SPI->>MCU: TSspi_Set_Poll() → TSspi_Set_Exchange(NOP)
        MCU-->>SPI: ack_seq / receipt
    end
    alt ack_seq 일치 및 receipt=0
        SPI->>CONTROL: TSspi_Set_Publish() → TScontrol_Set_Axis_Event(ACK)
        loop 실제 작업 완료까지 NOP (일반 작업 재전송 없음)
            SPI->>MCU: TSspi_Set_Exchange(NOP, 최신 remote)
            MCU-->>SPI: op_seq / state / fault / heartbeat
            SPI->>SPI: TSspi_Parse() → TSspi_Set_Receipt()
        end
        Note over SPI: op_seq 일치, COMPLETE, fault=0이어야 완료. position 비교 없음
        SPI->>CONTROL: TScontrol_Set_Axis_Event(COMPLETE)
    else receipt 거부 / 통신·Heartbeat·접수·작업 시간초과 / MCU 고장·재부팅
        SPI->>SPI: TSspi_Set_Fault() → TSspi_Set_Stop_All()
        SPI->>CONTROL: TScontrol_Set_Fault(SPI_COMMUNICATION)
        CONTROL->>CONTROL: TScontrol_Parse() → TScontrol_Set_Fault_Internal()
        CONTROL->>SPI: TScontrol_Set_Stop() → TSspi_Set_Axis_Command(STOP), 양축
        SPI->>SPI: TSspi_Set_Stop_Requests() (일반 큐보다 우선)
        loop STOP 확인까지 / 통신 가능할 때
            SPI->>MCU: TSspi_Set_Poll() → STOP 요청 (같은 seq 반복 가능)
            MCU-->>SPI: ack_seq 일치, receipt=0, UNKNOWN 또는 FAULT
        end
        Note over SPI,MCU: 응답 없이는 정지 확인 불가. 장치 재열기 후 감시·STOP만 재개
        CONTROL->>CONTROL: TScontrol_Set_Notice(ALARM)
    end
```
