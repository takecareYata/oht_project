/**
 * @file    loadport.c
 * @brief   [L3 Service] 로드포트 FOUP 감지·안착 판정 및 클램프
 * @details 1) 60ms마다 HC-SR04 측정
 *          2) 측정값 분류 : 있음 / 비어 있음 / 보류(중간 구간)
 *          3) 같은 분류가 3회 연속이면 상태 확정 (Loadport_Get_Status)
 *          4) 안착 : 확정 "있음" 상태에서 거리 변화 +-1cm 이내로
 *             1000ms 유지되면 안착 (Loadport_Get_Seat)
 *          5) 클램프 : 구동부 없음, 상태 플래그만 관리
 */
#include "loadport.h"
#include "hcsr04.h"
#include "tick.h"

static loadportStatus loadportConfirmed   = LOADPORT_STATUS_UNKNOWN;
static loadportStatus loadportCandidate   = LOADPORT_STATUS_UNKNOWN;
static uint8_t        loadportSameCount   = 0U;
static uint32_t       loadportMeasureTick = 0U;

static bool     loadportSeatTracking = false;
static bool     loadportSeated       = false;
static uint16_t loadportSeatRefMm    = 0U;
static uint32_t loadportSeatTick     = 0U;

static bool loadportClamp = false;

static loadportStatus Loadport_Get_Class(hcsr04Result result, uint16_t distanceMm);
static void           Loadport_Set_Confirm(loadportStatus rawStatus);
static void           Loadport_Set_Seat(loadportStatus rawStatus, uint16_t distanceMm, uint32_t nowTick);

void Loadport_Init_Sensor(void)
{
    loadportConfirmed    = LOADPORT_STATUS_UNKNOWN;
    loadportCandidate    = LOADPORT_STATUS_UNKNOWN;
    loadportSameCount    = 0U;
    loadportSeatTracking = false;
    loadportSeated       = false;
    loadportClamp        = false;

    Hcsr04_Init_Sensor();
    loadportMeasureTick = Tick_Get_Ms();
    Hcsr04_Set_Trigger();
}

/* 메인 루프에서 매번 호출 : 60ms마다 결과 처리 후 다음 측정 시작 */
void Loadport_Run_Sensor(void)
{
    uint32_t       nowTick = Tick_Get_Ms();
    uint16_t       distanceMm = 0U;
    hcsr04Result   result;
    loadportStatus rawStatus;

    if ((nowTick - loadportMeasureTick) >= LOADPORT_MEASURE_MS)
    {
        result = Hcsr04_Get_Distance(&distanceMm);

        if (result != HCSR04_RESULT_NONE)
        {
            rawStatus = Loadport_Get_Class(result, distanceMm);
            Loadport_Set_Confirm(rawStatus);
            Loadport_Set_Seat(rawStatus, distanceMm, nowTick);
        }

        loadportMeasureTick = nowTick;
        Hcsr04_Set_Trigger();
    }
}

loadportStatus Loadport_Get_Status(void)
{
    return loadportConfirmed;
}

/* 센서 현재 판정이 정상인지 : 최근 측정이 연속 3회 같은 유효값이고 확정값과 일치 (보류 구간 아님) */
bool Loadport_Get_Stable(void)
{
    return ((loadportCandidate != LOADPORT_STATUS_UNKNOWN) &&
            (loadportCandidate == loadportConfirmed) &&
            (loadportSameCount >= LOADPORT_CONFIRM_COUNT));
}

bool Loadport_Get_Seat(void)
{
    return loadportSeated;
}

void Loadport_Set_Clamp(bool on)
{
    loadportClamp = on;
}

bool Loadport_Get_Clamp(void)
{
    return loadportClamp;
}

/* 측정값 1회 분류 */
static loadportStatus Loadport_Get_Class(hcsr04Result result, uint16_t distanceMm)
{
    loadportStatus rawStatus = LOADPORT_STATUS_UNKNOWN;

    if (result == HCSR04_RESULT_NO_ECHO)
    {
        rawStatus = LOADPORT_STATUS_EMPTY;
    }
    else if (distanceMm <= LOADPORT_PRESENT_MM)
    {
        rawStatus = LOADPORT_STATUS_PRESENT;
    }
    else if (distanceMm >= LOADPORT_EMPTY_MM)
    {
        rawStatus = LOADPORT_STATUS_EMPTY;
    }
    else
    {
        rawStatus = LOADPORT_STATUS_UNKNOWN;   /* 중간 구간 : 보류 */
    }

    return rawStatus;
}

/* 3회 연속 동일 분류 시 확정, 보류 구간은 확정값 유지 */
static void Loadport_Set_Confirm(loadportStatus rawStatus)
{
    if (rawStatus == loadportCandidate)
    {
        if (loadportSameCount < LOADPORT_CONFIRM_COUNT)
        {
            loadportSameCount++;
        }
    }
    else
    {
        loadportCandidate = rawStatus;
        loadportSameCount = 1U;
    }

    if ((loadportSameCount >= LOADPORT_CONFIRM_COUNT) &&
        (rawStatus != LOADPORT_STATUS_UNKNOWN))
    {
        loadportConfirmed = rawStatus;
    }
}

/* 안착 판정 : 확정 "있음" + 거리 안정 + 유지 시간 */
static void Loadport_Set_Seat(loadportStatus rawStatus, uint16_t distanceMm, uint32_t nowTick)
{
    uint16_t diffMm;

    if ((loadportConfirmed == LOADPORT_STATUS_PRESENT) &&
        (rawStatus == LOADPORT_STATUS_PRESENT))
    {
        if (loadportSeatTracking == false)
        {
            loadportSeatTracking = true;
            loadportSeatRefMm    = distanceMm;
            loadportSeatTick     = nowTick;
        }
        else
        {
            diffMm = (distanceMm >= loadportSeatRefMm) ?
                     (uint16_t)(distanceMm - loadportSeatRefMm) :
                     (uint16_t)(loadportSeatRefMm - distanceMm);

            if (diffMm > LOADPORT_SEAT_TOL_MM)
            {
                /* 거리 변화 : FOUP 이동 중으로 보고 다시 측정 */
                loadportSeatRefMm = distanceMm;
                loadportSeatTick  = nowTick;
                loadportSeated    = false;
            }
            else if ((nowTick - loadportSeatTick) >= LOADPORT_SEAT_SETTLE_MS)
            {
                loadportSeated = true;
            }
            else
            {
                /* 유지 시간 대기 */
            }
        }
    }
    else
    {
        loadportSeatTracking = false;
        loadportSeated       = false;
    }
}
