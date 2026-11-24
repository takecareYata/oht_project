/* ============================================================================
 * config.h — 튜닝·설정 값 모음
 *
 * 방향 반전, 속도, 시간 제한, 스텝 수, 센서 극성, 빌드 모드를 여기서 바꾼다.
 * [미보정] 표시 값은 초기 추정치이므로 실물에서 측정해 수정할 것.
 * #ifndef로 감싼 값은 make 명령의 -D 옵션으로도 바꿀 수 있다.
 * ============================================================================ */
#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>
/* 보드 선택: BOARD_ID 1 = Z축·집게, 2 = X축 (Makefile의 BOARD=로 지정).
 * CONFIG_AXIS_ID = SPI 프레임의 axis 번호 (X = 1, Z = 2). */
#ifndef BOARD_ID
#define BOARD_ID 1
#endif
#if BOARD_ID == 1
#define CONFIG_AXIS_ID 2U
#elif BOARD_ID == 2
#define CONFIG_AXIS_ID 1U
#else
#error Unsupported BOARD_ID
#endif
/* ──────────── X축 (보드2) ──────────── */
/* 기준값은 TEST.STEP_MOTOR_L298N(TB6600 벤치 코드)과 같다:
 * 마이크로스텝 2, 1회전 = 200 × 2 = 400펄스, 최고 속도 1000 Hz.
 * CONFIG_X_MICROSTEP은 TB6600 DIP 스위치(SW1~3) 설정(1/2/4/8/16/32)과 반드시 일치시킨다. */
#define CONFIG_X_MICROSTEP 2U
#define CONFIG_X_PULSES_PER_REV (200UL * CONFIG_X_MICROSTEP)
/* A → B 거리(펄스). [미보정] 10회전.
 * 보정식: 이동거리 mm / 리드 mm/회전 × CONFIG_X_PULSES_PER_REV */
#define CONFIG_X_TRAVEL_STEPS (10UL * CONFIG_X_PULSES_PER_REV)
/* 원점 방식
 * 0: 센서 없음. 전원 인가 시 위치 0(= A)이라고 가정 → 켜기 전에 캐리지를 손으로 A에 둘 것.
 *    X_MOVE 0/1 = 스텝 0 / TRAVEL_STEPS로 이동.
 * 1: PC0 Home 센서 사용. 센서를 보기 전에는 위치 모름 → RECOVER(또는 X_MOVE 0)가
 *    CONFIG_X_HOME_HZ 저속으로 A를 찾아 그 자리를 0으로 잡는다.
 * 두 방식 모두 A/B 센서가 감지되면 멈추고 위치를 보정하므로, 센서를 나중에 달아도
 * 코드 변경이 필요 없다. */
#ifndef CONFIG_X_HOME_MODE
#define CONFIG_X_HOME_MODE 0U
#endif
/* 센서 탐색 이동이 TRAVEL_STEPS보다 이만큼(%) 더 가도 센서가 없으면 고장. */
#define CONFIG_X_OVERRUN_PERCENT 10UL
/* X 정거장 판정 백도어 (보드2, 벤치 시험 전용, 기본 0 = 꺼짐) — `make BOARD=2 X_STATION=1`
 * 0: relay bit2(X 유효 정거장)는 A/B 끝 센서가 실제로 감지될 때만 켠다.
 * 1: 센서가 없어도, 원점이 잡혀 있고 정지 상태이며 스텝 위치가 정확히 0(A) 또는
 *    CONFIG_X_TRAVEL_STEPS(B)이면 bit2를 켠다. → X 센서 없이 Z PICK/PLACE가 거부되지 않는다.
 * 오픈루프라 탈조하면 실제 위치와 어긋나도 알 수 없다. 켜기 전에 캐리지를 손으로 A에 둘 것.
 * 센서가 감지되면 그것도 그대로 반영된다. 백도어 빌드는 상태 LED가 100 ms로 빠르게 깜빡인다. */
#ifndef CONFIG_X_STATION_STEPS
#define CONFIG_X_STATION_STEPS 0U
#endif
/* ──────────── 공통 시간 값 ──────────── */
#define CONFIG_Z_TRAVEL_MS 10000UL        /* [미보정] Z 전체 스트로크 시간 (위치 표시용 추정) */
#define CONFIG_CLOCK_HZ 16000000UL        /* 코어 클록: 리셋 기본 HSI 16 MHz, PLL 없음 */
#define CONFIG_COMM_TIMEOUT_MS 500UL      /* ARM 중 유효 요청이 이만큼 없으면 LINK 정지 */
#define CONFIG_MOVE_TIMEOUT_MS 15000UL    /* X·Z 이동 최대 시간 → 넘으면 TIMEOUT */
#define CONFIG_RELEASE_TIMEOUT_MS 1500UL  /* 출발 센서가 이 시간 뒤에도 감지 → SENSOR */
#define CONFIG_SENSOR_DEBOUNCE_MS 10UL    /* 센서 값이 이만큼 안정돼야 반영 */
#define CONFIG_GRIP_TIMEOUT_MS 3000UL     /* 집게 열기/닫기 최대 시간 → 넘으면 GRIP */
/* ──────────── 집게 (보드1) ────────────
 * 28BYJ-48 + ULN2003 (IN1~IN4 = PA8, PA9, PA10, PB4), 하프스텝, 출력축 1회전 ≈ 4096스텝.
 * 스텝 0 = 열림이며 전원 인가 시 열려 있다고 가정 → Z 보드 켜기 전에 손으로 열 것.
 * 닫기는 PC2 접촉 스위치까지 진행, CONFIG_GRIP_CLOSE_MAX_STEPS 안에 접촉이 없으면 GRIP 고장.
 * 열기는 스텝 0으로 복귀. */
#define CONFIG_GRIP_STEP_MS 2UL              /* 스텝 간격: 2 ms = 초당 500 하프스텝 */
#define CONFIG_GRIP_CLOSE_MAX_STEPS 1024UL   /* [미보정] 닫기 최대 스텝 = 출력축 1/4회전 */
#define CONFIG_GRIP_REVERSE 0U               /* 닫는 방향이 반대로 돌면 1 */
/* ──────────── Z 백도어 (보드1, 벤치 시험 전용, 기본 0 = 꺼짐) ────────────
 * Z 포토인터럽터와 집게 접촉 스위치 없이 PICK/PLACE를 돌리기 위한 모드.
 *  1 (`make BOARD=1 Z_TIMED=1`): 시간 모드. Z 상승/하강은 CONFIG_Z_UP_MS / CONFIG_Z_DOWN_MS
 *    구동 후 도착, 집게 닫기는 CONFIG_GRIP_CLOSE_STEPS 스텝 후 접촉으로 간주. B1 버튼도 동작.
 *  2 (`make BOARD=1 Z_TIMED=2`): 버튼 모드. Nucleo 파란 버튼 B1(PC13)을 누르면 진행 중인
 *    단계(Z 도착 또는 집게 접촉)를 완료로 넘긴다. 집게 열기는 스텝 0에서 스스로 끝난다.
 *    Z는 15초 안에 눌러야 한다. 집게 닫기는 CONFIG_GRIP_CLOSE_STEPS까지만 닫고 멈춘 채
 *    B1을 기다린다(최대 CONFIG_MOVE_TIMEOUT_MS = 15초, 넘으면 GRIP 고장).
 * 결과는 가상 PC0/PC1/PC2 입력으로 들어가므로 PICK/PLACE, 인터록, 위치 표시는 그대로 동작하고
 * 실제 센서를 달면 그것도 반영된다. 실제 위치는 확인하지 않으므로 액추에이터가 끝에서
 * 스스로 멈춰야 한다(내장 리밋). 전원 인가 시 Z 위치는 모름(첫 RECOVER를 시간/버튼으로
 * 완료해야 함), 집게는 열림으로 가정. 백도어 빌드는 상태 LED가 100 ms로 빠르게 깜빡인다. */
#ifndef CONFIG_Z_TIMED
#define CONFIG_Z_TIMED 0U
#endif
#define CONFIG_Z_UP_MS 10000UL               /* [미보정] 상승 시간: 전체 스트로크 + 여유 */
#define CONFIG_Z_DOWN_MS 10000UL             /* [미보정] 하강 시간: 전체 스트로크 + 여유 */
#define CONFIG_GRIP_CLOSE_STEPS 512UL        /* [미보정] 닫기 스텝 수 (약 1초) */
#define CONFIG_BUTTON_DEBOUNCE_MS 20UL       /* B1 버튼 디바운스 */
/* ──────────── X 속도 (TB6600 PUL 입력 기준 Hz) ────────────
 * 최고 속도 = 기준 코드의 STEP_US. 기준 코드는 1000 Hz로 바로 시작하지만, 여기서는
 * 절반 속도로 출발해 약 250 ms 동안 가속하고, 목표 펄스 전에 감속한다. */
#define CONFIG_X_STEP_US 1000U
#define CONFIG_X_STEP_HZ (1000000U / CONFIG_X_STEP_US)
#define CONFIG_X_START_HZ (CONFIG_X_STEP_HZ / 2U)
#define CONFIG_X_ACCEL_HZ 40U /* CONFIG_X_ACCEL_INTERVAL_MS마다 더하고 빼는 Hz */
#define CONFIG_X_ACCEL_INTERVAL_MS 20UL
/* 센서 탐색 이동 속도 (HOME_MODE 1, 원점 잡기 전) */
#define CONFIG_X_HOME_HZ CONFIG_X_START_HZ
/* bsp_Set_Step_Timer가 받는 최대 주파수 (TB6600 한계보다 충분히 낮음) */
#define CONFIG_X_MAX_PULSE_HZ 20000U
/* ──────────── 방향·극성 ────────────
 * 논리 0 = A(홈)·Z 상승, 논리 1 = B(로드포트)·Z 하강. 실제 회전이 반대면 1로 바꾼다. */
#define CONFIG_X_REVERSE 0U
#define CONFIG_Z_REVERSE 0U
/* PC0/1/2는 내부 풀업 + Low = 감지. 1이 아니면 세 입력 모두 High = 감지로 바뀐다. */
#define CONFIG_SENSOR_ACTIVE_LOW 1U
/* X 정지 시 코일 유지(홀드). 기본 0 = 정지하면 해제. 전류·온도 확인 후에만 1로. */
#define CONFIG_X_HOLD_ON_STOP 0U
/* 컴파일 시 설정값 범위 검사: 백도어 완료가 안전 제한보다 먼저 와야 한다 */
#if (CONFIG_Z_UP_MS >= CONFIG_MOVE_TIMEOUT_MS) || (CONFIG_Z_DOWN_MS >= CONFIG_MOVE_TIMEOUT_MS)
#error CONFIG_Z_UP_MS/CONFIG_Z_DOWN_MS must be below CONFIG_MOVE_TIMEOUT_MS
#endif
#if (CONFIG_GRIP_CLOSE_STEPS >= CONFIG_GRIP_CLOSE_MAX_STEPS) || \
    ((CONFIG_GRIP_CLOSE_STEPS * CONFIG_GRIP_STEP_MS) >= CONFIG_GRIP_TIMEOUT_MS)
#error CONFIG_GRIP_CLOSE_STEPS must finish before the gripper step and time limits
#endif
#endif
