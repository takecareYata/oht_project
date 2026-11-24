/**
 * @file    e84.c
 * @brief   [L3 Service] E84 Passive FSM
 * @details 적재(LOAD)
 *            VALID+CS_0 ON, 포트 비어 있음 -> L_REQ ON
 *            TR_REQ ON  -> READY ON
 *            BUSY ON    -> (OHT 하강) 안착 판정 -> L_REQ OFF
 *            BUSY OFF, TR_REQ OFF, COMPT ON -> READY OFF
 *            VALID, CS_0, COMPT OFF -> 클램프 ON -> IDLE
 *          반출(UNLOAD)
 *            VALID+CS_0 ON, FOUP 있음 -> 클램프 OFF -> U_REQ ON
 *            이후 동일, 안착 판정 대신 "비어 있음" 확정 시 U_REQ OFF
 *          Fault
 *            타임아웃 또는 순서 위반 -> L_REQ, U_REQ, READY, HO_AVBL OFF
 *            (ES는 STM32③에 E-STOP 입력이 없어 ON 유지)
 *          Fault 복구 (Jetson 중앙제어 RECOVER 방식)
 *            Jetson : RECOVER -> E84 출력 5개 모두 LOW
 *            LP     : 입력 5개 모두 OFF가 1초 유지 + 센서 현재 판정 정상(보류 구간 아님)
 *                     -> Fault 해제, HO_AVBL ON, IDLE
 */
#include "e84.h"
#include "pio.h"
#include "loadport.h"
#include "system.h"
#include "tick.h"

#define E84_OUT_FAULT_OFF   (PIO_OUT_L_REQ | PIO_OUT_U_REQ | PIO_OUT_READY | PIO_OUT_HO_AVBL)

static e84State e84CurState    = E84_STATE_IDLE;
static e84Mode  e84CurMode     = E84_MODE_NONE;
static uint8_t  e84Input       = 0U;   /* 디바운스된 입력 */
static uint8_t  e84PrevRaw     = 0U;
static uint32_t e84SampleTick  = 0U;
static uint32_t e84StateTick   = 0U;
static bool     e84RecoverHold = false;   /* Fault 중 입력 전체 OFF 유지 중 */
static uint32_t e84RecoverTick = 0U;      /* 입력 전체 OFF 시작 시각 */

static void        E84_Set_State(e84State nextState);
static bool        E84_Get_Bit(uint8_t inMask);
static void        E84_Set_Fault(systemError errorCode);
static systemError E84_Get_Error(void);

/* GPIO 설정 -> 출력 전체 OFF -> HO_AVBL, ES ON (인계 가능 상태) */
void E84_Init_Pio(void)
{
    Pio_Init_Gpio();      /* E84 핀 설정, 출력 전체 OFF */

    e84PrevRaw    = Pio_Get_Input();
    e84Input      = 0U;
    e84SampleTick = Tick_Get_Ms();
    e84CurMode    = E84_MODE_NONE;
    E84_Set_State(E84_STATE_IDLE);

    E84_Set_Output((uint8_t)(PIO_OUT_HO_AVBL | PIO_OUT_ES), true);
}

/* 1ms마다 입력을 읽고 2회 연속 같은 값일 때만 반영 */
void E84_Get_Input(void)
{
    uint32_t nowTick = Tick_Get_Ms();
    uint8_t  rawInput;

    if ((nowTick - e84SampleTick) >= E84_SAMPLE_MS)
    {
        rawInput = Pio_Get_Input();

        if (rawInput == e84PrevRaw)
        {
            e84Input = rawInput;
        }

        e84PrevRaw    = rawInput;
        e84SampleTick = nowTick;
    }
}

void E84_Set_Output(uint8_t outMask, bool on)
{
    Pio_Set_Output(outMask, on);
}

/* 현재 단계의 TP 타임아웃 초과 여부 */
bool E84_Get_Timeout(void)
{
    uint32_t limitMs;

    switch (e84CurState)
    {
        case E84_STATE_WAIT_TR_REQ:    limitMs = E84_TP1_MS; break;
        case E84_STATE_WAIT_BUSY:      limitMs = E84_TP2_MS; break;
        case E84_STATE_WAIT_TRANSFER:  limitMs = E84_TP3_MS; break;
        case E84_STATE_WAIT_COMPLETE:  limitMs = E84_TP4_MS; break;
        case E84_STATE_WAIT_VALID_OFF: limitMs = E84_TP5_MS; break;
        default:                       limitMs = 0U;         break;
    }

    return ((limitMs != 0U) && ((Tick_Get_Ms() - e84StateTick) >= limitMs));
}

e84State E84_Get_State(void)
{
    return e84CurState;
}

e84Mode E84_Get_Mode(void)
{
    return e84CurMode;
}

void E84_Run_Fsm(void)
{
    bool valid     = E84_Get_Bit(PIO_IN_VALID);
    bool cs0       = E84_Get_Bit(PIO_IN_CS_0);
    bool trReq     = E84_Get_Bit(PIO_IN_TR_REQ);
    bool busy      = E84_Get_Bit(PIO_IN_BUSY);
    bool compt     = E84_Get_Bit(PIO_IN_COMPT);
    bool handshake = (valid && cs0);
    loadportStatus portStatus;

    if (E84_Get_Timeout() == true)
    {
        E84_Set_Fault(E84_Get_Error());
    }
    else
    {
        switch (e84CurState)
        {
            case E84_STATE_IDLE:
                if (handshake && (!trReq) && (!busy) && (!compt))
                {
                    portStatus = Loadport_Get_Status();

                    if (portStatus == LOADPORT_STATUS_EMPTY)
                    {
                        e84CurMode = E84_MODE_LOAD;
                        E84_Set_Output(PIO_OUT_L_REQ, true);
                        E84_Set_State(E84_STATE_WAIT_TR_REQ);
                    }
                    else if (portStatus == LOADPORT_STATUS_PRESENT)
                    {
                        e84CurMode = E84_MODE_UNLOAD;
                        Loadport_Set_Clamp(false);
                        E84_Set_Output(PIO_OUT_U_REQ, true);
                        E84_Set_State(E84_STATE_WAIT_TR_REQ);
                    }
                    else
                    {
                        /* 센서 판정 전 : 응답하지 않고 대기 */
                    }
                }
                break;

            case E84_STATE_WAIT_TR_REQ:
                if (!handshake)
                {
                    E84_Set_Fault(SYSTEM_ERR_SEQUENCE);
                }
                else if (trReq)
                {
                    E84_Set_Output(PIO_OUT_READY, true);
                    E84_Set_State(E84_STATE_WAIT_BUSY);
                }
                else
                {
                    /* TR_REQ 대기 */
                }
                break;

            case E84_STATE_WAIT_BUSY:
                if ((!handshake) || (!trReq))
                {
                    E84_Set_Fault(SYSTEM_ERR_SEQUENCE);
                }
                else if (busy)
                {
                    E84_Set_State(E84_STATE_WAIT_TRANSFER);
                }
                else
                {
                    /* BUSY 대기 */
                }
                break;

            case E84_STATE_WAIT_TRANSFER:
                if ((!handshake) || (!busy))
                {
                    E84_Set_Fault(SYSTEM_ERR_SEQUENCE);
                }
                else if ((e84CurMode == E84_MODE_LOAD) && (Loadport_Get_Seat() == true))
                {
                    E84_Set_Output(PIO_OUT_L_REQ, false);
                    E84_Set_State(E84_STATE_WAIT_COMPLETE);
                }
                else if ((e84CurMode == E84_MODE_UNLOAD) &&
                         (Loadport_Get_Status() == LOADPORT_STATUS_EMPTY))
                {
                    E84_Set_Output(PIO_OUT_U_REQ, false);
                    E84_Set_State(E84_STATE_WAIT_COMPLETE);
                }
                else
                {
                    /* 안착/제거 판정 대기 */
                }
                break;

            case E84_STATE_WAIT_COMPLETE:
                if (!handshake)
                {
                    E84_Set_Fault(SYSTEM_ERR_SEQUENCE);
                }
                else if ((!busy) && (!trReq) && compt)
                {
                    E84_Set_Output(PIO_OUT_READY, false);
                    E84_Set_State(E84_STATE_WAIT_VALID_OFF);
                }
                else
                {
                    /* BUSY OFF, TR_REQ OFF, COMPT ON 대기 */
                }
                break;

            case E84_STATE_WAIT_VALID_OFF:
                if ((!valid) && (!cs0) && (!compt))
                {
                    if (e84CurMode == E84_MODE_LOAD)
                    {
                        Loadport_Set_Clamp(true);
                    }
                    e84CurMode = E84_MODE_NONE;
                    E84_Set_State(E84_STATE_IDLE);
                }
                break;

            case E84_STATE_FAULT:
                /* 복구 : 입력 5개가 모두 OFF인 상태로 E84_RECOVER_MS 유지 + 센서 현재 판정 정상 */
                if (e84Input == 0U)
                {
                    if (e84RecoverHold == false)
                    {
                        e84RecoverHold = true;
                        e84RecoverTick = Tick_Get_Ms();
                    }
                    else if (((Tick_Get_Ms() - e84RecoverTick) >= E84_RECOVER_MS) &&
                             (Loadport_Get_Stable() == true))
                    {
                        System_Clear_Fault();
                        e84RecoverHold = false;
                        e84CurMode     = E84_MODE_NONE;
                        E84_Set_Output(PIO_OUT_HO_AVBL, true);
                        E84_Set_State(E84_STATE_IDLE);
                    }
                    else
                    {
                        /* 유지 시간 또는 센서 판정 대기 */
                    }
                }
                else
                {
                    e84RecoverHold = false;   /* 입력이 하나라도 ON이면 처음부터 다시 */
                }
                break;

            default:
                E84_Set_Fault(SYSTEM_ERR_SEQUENCE);
                break;
        }
    }
}

static void E84_Set_State(e84State nextState)
{
    e84CurState  = nextState;
    e84StateTick = Tick_Get_Ms();
}

static bool E84_Get_Bit(uint8_t inMask)
{
    return ((e84Input & inMask) != 0U);
}

static void E84_Set_Fault(systemError errorCode)
{
    System_Set_Fault(errorCode);
    E84_Set_Output((uint8_t)E84_OUT_FAULT_OFF, false);
    e84CurMode = E84_MODE_NONE;
    E84_Set_State(E84_STATE_FAULT);
}

/* 현재 단계에 해당하는 타임아웃 에러 코드 */
static systemError E84_Get_Error(void)
{
    systemError errorCode;

    switch (e84CurState)
    {
        case E84_STATE_WAIT_TR_REQ:    errorCode = SYSTEM_ERR_TP1_TIMEOUT; break;
        case E84_STATE_WAIT_BUSY:      errorCode = SYSTEM_ERR_TP2_TIMEOUT; break;
        case E84_STATE_WAIT_TRANSFER:  errorCode = SYSTEM_ERR_TP3_TIMEOUT; break;
        case E84_STATE_WAIT_COMPLETE:  errorCode = SYSTEM_ERR_TP4_TIMEOUT; break;
        case E84_STATE_WAIT_VALID_OFF: errorCode = SYSTEM_ERR_TP5_TIMEOUT; break;
        default:                       errorCode = SYSTEM_ERR_SEQUENCE;    break;
    }

    return errorCode;
}
