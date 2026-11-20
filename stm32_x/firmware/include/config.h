#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>
/* BOARD_ID: 1 = Z/gripper, 2 = X. HSI reset clock, no PLL. */
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
/* Diagnostic estimates only: calibrate these against the actual full travel. */
/* X step numbers follow TEST.STEP_MOTOR_L298N (TB6600 bench code):
 * MICROSTEP 2, PULSES_PER_REV = 200 * MICROSTEP = 400, STEP_US 1000 (1000 Hz).
 * TB6600 DIP microstep setting (1, 2, 4, 8, 16, 32). Must match SW1..SW3. */
#define CONFIG_X_MICROSTEP 2U
#define CONFIG_X_PULSES_PER_REV (200UL * CONFIG_X_MICROSTEP)
/* A -> B distance in driver pulses. UNCALIBRATED: 10 revolutions.
 * Calibrate: travel_mm / lead_mm_per_rev * CONFIG_X_PULSES_PER_REV. */
#define CONFIG_X_TRAVEL_STEPS (10UL * CONFIG_X_PULSES_PER_REV)
/* 0: no sensors. Position 0 (= A) is assumed at power-on: place the carriage
 *    at A by hand before powering up. X_MOVE 0/1 goes to step 0 / TRAVEL_STEPS.
 * 1: home sensor on PC0. Position is unknown until a sensor is seen; RECOVER
 *    (or X_MOVE 0) seeks A at CONFIG_X_HOME_HZ and sets position 0 there.
 * In both modes a detected A/B sensor always stops the move and corrects the
 * position, so installing sensors later needs no code change. */
#ifndef CONFIG_X_HOME_MODE
#define CONFIG_X_HOME_MODE 0U
#endif
/* Sensor-seek moves fault if they run this much past TRAVEL_STEPS. */
#define CONFIG_X_OVERRUN_PERCENT 10UL
#define CONFIG_Z_TRAVEL_MS 10000UL
#define CONFIG_CLOCK_HZ 16000000UL
#define CONFIG_COMM_TIMEOUT_MS 500UL
#define CONFIG_MOVE_TIMEOUT_MS 15000UL
#define CONFIG_RELEASE_TIMEOUT_MS 1500UL
#define CONFIG_SENSOR_DEBOUNCE_MS 10UL
#define CONFIG_GRIP_TIMEOUT_MS 3000UL
#define CONFIG_SERVO_SETTLE_MS 700UL
#define CONFIG_SERVO_OPEN_US 1100U
#define CONFIG_SERVO_CLOSED_US 1800U
#define CONFIG_SERVO_INCREMENT_US 5U
#define CONFIG_SERVO_PERIOD_MS 20UL
/* Pulse rates in Hz at the TB6600 PUL input. Top speed = reference STEP_US.
 * The reference starts at 1000 Hz with no ramp; here moves start at half that,
 * ramp up in ~250 ms and ramp down before the target step. */
#define CONFIG_X_STEP_US 1000U
#define CONFIG_X_STEP_HZ (1000000U / CONFIG_X_STEP_US)
#define CONFIG_X_START_HZ (CONFIG_X_STEP_HZ / 2U)
#define CONFIG_X_ACCEL_HZ 40U /* added/removed every CONFIG_X_ACCEL_INTERVAL_MS */
#define CONFIG_X_ACCEL_INTERVAL_MS 20UL
/* Constant speed while seeking a sensor (HOME_MODE 1 before referencing). */
#define CONFIG_X_HOME_HZ CONFIG_X_START_HZ
/* Upper bound accepted by bsp_Set_Step_Timer (TB6600 PUL limit is well above this). */
#define CONFIG_X_MAX_PULSE_HZ 20000U
#define CONFIG_Z_DUTY_PERMILLE 700U
/* Logical 0 = A/home or Z up; logical 1 = B/port or Z down. */
#define CONFIG_X_REVERSE 0U
#define CONFIG_Z_REVERSE 0U
/* PC0/1/2 are active low, pull-up. Change sensor.c for other polarity. */
#define CONFIG_SENSOR_ACTIVE_LOW 1U
/* X hold is disabled by default; set after current/temperature validation. */
#define CONFIG_X_HOLD_ON_STOP 0U
#endif
