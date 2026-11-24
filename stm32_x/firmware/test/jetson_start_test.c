#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * jetson_start_test.c — START 시퀀스 1회 시험 (SPI 프로토콜 v2, 24바이트)
 *
 * 규약 6장 "상위 시퀀스"를 그대로 한 번 실행한다.
 *   시작·복구: NOP(seq 동기화) → STOP → ARM(양축) → Z RECOVER → X RECOVER
 *   반송    : Z PICK → X X_MOVE(1) → Z PLACE → (기본) X X_MOVE(0)로 A 복귀
 * 어느 단계든 실패하면 양축에 STOP을 보내고 종료한다. Ctrl+C도 STOP 후 종료.
 *
 * remote는 규약대로 상대 축 최신 응답의 relay에서 만든다 (가짜 값 아님).
 *   X 요청 remote = Z 응답 byte9 & 0x01,  Z 요청 remote = X 응답 byte9 & 0x06
 *   상대 응답이 300 ms보다 오래되면 0.
 * 따라서 센서 없이 돌리려면 STM32 쪽이 백도어 빌드여야 한다.
 *   Z 보드: make BOARD=1 Z_TIMED=2   (B1 버튼으로 단계 완료)
 *   X 보드: make BOARD=2 X_STATION=1 (스텝 위치로 정거장 비트)
 * 전원 인가 전: X 캐리지는 A, 집게는 열림. 모터 전원 스위치를 손 닿는 곳에.
 *
 * 빌드(Jetson): gcc -O2 -Wall -Wextra -o jetson_start_test jetson_start_test.c
 * 실행       : sudo ./jetson_start_test [-s 1000000] [-n] [-v]
 *   -s HZ  SPI 속도 (기본 1000000)
 *   -n     마지막 X A 복귀 생략
 *   -v     모든 송수신 HEX 출력
 * 벤치 시험 전용.
 * ============================================================================ */

#define FRAME 24
#define CMD_NOP 0
#define CMD_ARM 1
#define CMD_STOP 2
#define CMD_RECOVER 3
#define CMD_X_MOVE 16
#define CMD_PICK 36
#define CMD_PLACE 37
#define ST_UNKNOWN 0
#define ST_IDLE 1
#define ST_MOVING 2
#define ST_COMPLETE 3
#define ST_FAULT 4

#define POLL_GAP_MS 10        /* 트랜잭션 사이 간격 (CS HIGH ≥ 1 ms 보장) */
#define RESPONSE_DELAY_MS 1   /* 요청 후 NOP까지 대기 (규약 3장) */
#define VALID_TIMEOUT_S 0.5   /* 유효 응답 제한시간 */
#define ACCEPT_TIMEOUT_S 0.5  /* 수락 제한시간 */
#define HB_TIMEOUT_S 1.0      /* heartbeat 변화 제한시간 */
#define REMOTE_STALE_S 0.3    /* 상대 응답이 이보다 오래되면 remote = 0 */
#define MOVE_TIMEOUT_S 30.0   /* RECOVER / X_MOVE */
#define PICK_TIMEOUT_S 45.0   /* PICK / PLACE */

typedef struct
{
    const char *name;
    const char *dev;
    uint8_t axis;
    int fd;
    uint16_t seq;          /* 마지막으로 보낸 seq */
    bool have;             /* last[]가 유효 응답인가 */
    uint8_t last[FRAME];   /* 최신 유효 응답 */
    double valid_at;       /* 최신 유효 응답 시각 */
    uint16_t hb;           /* 마지막 heartbeat */
    double hb_at;          /* heartbeat가 마지막으로 바뀐 시각 */
    unsigned invalid;      /* 깨진 응답 수 */
    int shown_state, shown_stage;
} board_t;

static board_t X = {"X", "/dev/spidev0.0", 1, -1, 0, false, {0}, 0, 0, 0, 0, -1, -1};
static board_t Z = {"Z", "/dev/spidev0.1", 2, -1, 0, false, {0}, 0, 0, 0, 0, -1, -1};
static uint32_t speed_hz = 1000000;
static bool verbose;
static volatile sig_atomic_t interrupted;

/* ---------------------------------------------------------------- 유틸 */
static void on_signal(int sig) { (void)sig; interrupted = 1; }
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static void sleep_ms(unsigned ms)
{
    struct timespec ts = {ms / 1000U, (long)(ms % 1000U) * 1000000L};
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i)
    {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (unsigned j = 0; j < 8; ++j)
            crc = (uint16_t)((crc & 0x8000U) ? (((unsigned)crc << 1) ^ 0x1021U) : ((unsigned)crc << 1));
    }
    return crc;
}
static uint16_t next_seq(uint16_t s) { return (s == 0xFFFFU) ? 1U : (uint16_t)(s + 1U); }
static const char *state_name(int s)
{
    static const char *n[] = {"UNKNOWN", "IDLE", "MOVING", "COMPLETE", "FAULT"};
    return (s >= 0 && s <= 4) ? n[s] : "?";
}
static const char *error_name(int e)
{
    static const char *n[] = {"OK", "STOP", "LINK", "SENSOR", "TIMEOUT", "INTERLOCK",
                              "GRIP", "BUSY", "COMMAND", "SEQUENCE", "TRANSPORT"};
    return (e >= 0 && e <= 10) ? n[e] : "?";
}
static const char *cmd_name(uint8_t c)
{
    switch (c)
    {
    case CMD_NOP: return "NOP";
    case CMD_ARM: return "ARM";
    case CMD_STOP: return "STOP";
    case CMD_RECOVER: return "RECOVER";
    case CMD_X_MOVE: return "X_MOVE";
    case CMD_PICK: return "PICK";
    case CMD_PLACE: return "PLACE";
    default: return "?";
    }
}
static void hex(const char *tag, const board_t *b, const uint8_t *p)
{
    printf("    [%s] %s:", b->name, tag);
    for (unsigned i = 0; i < FRAME; ++i) printf(" %02X", p[i]);
    printf("\n");
}

/* Z 보드 백도어(버튼 모드) 안내: stage별로 사람이 할 일 */
static const char *z_stage_hint(int stage)
{
    switch (stage)
    {
    case 3: return "Z 상승 중 → 위에 닿으면 Z 보드 B1 (이미 위면 바로)";
    case 4: return "PICK 하강 중 → 아래 닿으면 Z 보드 B1";
    case 5: return "PICK 집게 열기 (자동)";
    case 6: return "PICK 집게 닫기 → 다 닫히면 Z 보드 B1 (약 2초 안에)";
    case 7: return "PICK 상승 중 → 위에 닿으면 Z 보드 B1";
    case 8: return "PLACE 하강 중 → 아래 닿으면 Z 보드 B1";
    case 9: return "PLACE 집게 열기 (자동)";
    case 10: return "PLACE 상승 중 → 위에 닿으면 Z 보드 B1";
    default: return "";
    }
}

/* ---------------------------------------------------------------- SPI */
/* 상대 축 최신 응답의 relay로 remote 계산 (규약 6장) */
static uint8_t remote_for(const board_t *b)
{
    const board_t *peer = (b == &X) ? &Z : &X;
    uint8_t mask = (b == &X) ? 0x01U : 0x06U;
    if (!peer->have || (now() - peer->valid_at) > REMOTE_STALE_S) return 0U;
    return (uint8_t)(peer->last[9] & mask);
}

/* 응답 검사. STM32가 MISO를 NSS 인터럽트에서 켜서 첫 바이트가 깨질 수 있으므로
 * 고정 헤더(5A 02 axis)는 복원하고 CRC로 최종 판정한다. */
static bool check_response(board_t *b, uint8_t *rx)
{
    rx[0] = 0x5A; rx[1] = 2; rx[2] = b->axis;
    if (rx[17] || rx[20] || rx[21]) return false;
    if (u16(rx + 22) != crc16(rx, 22)) return false;
    if (rx[3] > 4 || rx[6] > 10 || rx[7] > 10) return false;
    return true;
}

static void show_changes(board_t *b)
{
    int state = b->last[3], stage = (b->last[9] >> 3) & 15;
    if (state == b->shown_state && stage == b->shown_stage) return;
    printf("  [%s] state=%s stage=%d fault=%s op_seq=%u relay=0x%X sensors=0x%02X\n", b->name,
           state_name(state), stage, error_name(b->last[7]), u16(b->last + 10), b->last[9] & 7U,
           b->last[8]);
    if (b == &Z && state == ST_MOVING && z_stage_hint(stage)[0] != '\0')
        printf("  >>> %s\n", z_stage_hint(stage));
    b->shown_state = state;
    b->shown_stage = stage;
}

/* 24바이트 1회 송수신. 유효 응답이면 last[] 갱신. 반환: 1 유효, 0 깨짐, -1 SPI 오류 */
static int xfer(board_t *b, uint8_t cmd, uint16_t seq, uint8_t arg)
{
    uint8_t tx[FRAME] = {0}, rx[FRAME] = {0};
    struct spi_ioc_transfer tr;
    tx[0] = 0xA5; tx[1] = 2; tx[2] = b->axis; tx[3] = cmd;
    put16(tx + 4, seq); tx[6] = arg; tx[7] = remote_for(b);
    put16(tx + 22, crc16(tx, 22));
    memset(&tr, 0, sizeof tr);
    tr.tx_buf = (unsigned long)(uintptr_t)tx;
    tr.rx_buf = (unsigned long)(uintptr_t)rx;
    tr.len = FRAME;
    tr.speed_hz = speed_hz;
    tr.bits_per_word = 8;
    if (ioctl(b->fd, SPI_IOC_MESSAGE(1), &tr) != FRAME)
    {
        fprintf(stderr, "[%s] SPI ioctl 실패: %s\n", b->name, strerror(errno));
        return -1;
    }
    if (verbose) { hex("TX", b, tx); hex("RX", b, rx); }
    if (!check_response(b, rx))
    {
        b->invalid++;
        if (verbose) printf("    [%s] 깨진 응답 (누적 %u)\n", b->name, b->invalid);
        return 0;
    }
    memcpy(b->last, rx, FRAME);
    b->have = true;
    b->valid_at = now();
    if (u16(rx + 18) != b->hb) { b->hb = u16(rx + 18); b->hb_at = b->valid_at; }
    show_changes(b);
    return 1;
}

/* 양축 NOP 한 바퀴 (링크 유지 + remote 갱신). 문제 있으면 사유 문자열 반환 */
static const char *poll_both(void)
{
    board_t *bs[2] = {&X, &Z};
    for (int i = 0; i < 2; ++i)
    {
        if (xfer(bs[i], CMD_NOP, 0, 0) < 0) return "SPI 오류";
        sleep_ms(POLL_GAP_MS);
        if (now() - bs[i]->valid_at > VALID_TIMEOUT_S) return bs[i] == &X ? "X 유효 응답 없음" : "Z 유효 응답 없음";
        if (now() - bs[i]->hb_at > HB_TIMEOUT_S) return bs[i] == &X ? "X heartbeat 멈춤" : "Z heartbeat 멈춤";
    }
    return NULL;
}

/* 명령 전송 → 수락 확인(ack_seq = seq, receipt = 0). 실패 사유 반환 */
static const char *send_command(board_t *b, uint8_t cmd, uint8_t arg)
{
    double start;
    b->seq = next_seq(b->seq);
    printf("\n>>> [%s] %s arg=%u seq=%u remote=0x%X\n", b->name, cmd_name(cmd), arg, b->seq,
           remote_for(b));
    if (xfer(b, cmd, b->seq, arg) < 0) return "SPI 오류";
    sleep_ms(RESPONSE_DELAY_MS);
    start = now();
    while (!interrupted)
    {
        const char *e = poll_both();
        if (e) return e;
        if (b->have && u16(b->last + 4) == b->seq)
        {
            uint8_t receipt = b->last[6];
            if (receipt != 0U)
            {
                static char msg[64];
                snprintf(msg, sizeof msg, "%s 거부: receipt=%s", cmd_name(cmd), error_name(receipt));
                return msg;
            }
            printf("  [%s] %s 수락\n", b->name, cmd_name(cmd));
            return NULL;
        }
        if (now() - start > ACCEPT_TIMEOUT_S) return "수락 확인 시간 초과 (ack_seq 불일치)";
    }
    return "중단됨 (Ctrl+C)";
}

/* 작업 명령 완료 대기: op_seq = seq, state = COMPLETE, fault = 0 (규약 4장) */
static const char *wait_complete(board_t *b, double timeout_s)
{
    double start = now();
    while (!interrupted)
    {
        const char *e = poll_both();
        if (e) return e;
        if (b->last[3] == ST_FAULT)
        {
            static char msg[64];
            snprintf(msg, sizeof msg, "%s FAULT: fault=%s", b->name, error_name(b->last[7]));
            return msg;
        }
        if (u16(b->last + 10) == b->seq && b->last[3] == ST_COMPLETE && b->last[7] == 0U)
        {
            printf("  [%s] 완료 (%.1f s)\n", b->name, now() - start);
            return NULL;
        }
        if (now() - start > timeout_s) return "완료 대기 시간 초과";
    }
    return "중단됨 (Ctrl+C)";
}

static const char *run_job(board_t *b, uint8_t cmd, uint8_t arg, double timeout_s)
{
    const char *e = send_command(b, cmd, arg);
    return e ? e : wait_complete(b, timeout_s);
}

/* 양축 STOP (실패 시 정리용, 응답 확인은 최선만) */
static void stop_both(void)
{
    board_t *bs[2] = {&X, &Z};
    printf("\n>>> 양축 STOP\n");
    for (int i = 0; i < 2; ++i)
    {
        bs[i]->seq = next_seq(bs[i]->seq);
        (void)xfer(bs[i], CMD_STOP, bs[i]->seq, 0);
        sleep_ms(POLL_GAP_MS);
    }
    for (int k = 0; k < 10; ++k) (void)poll_both();
}

/* ---------------------------------------------------------------- 메인 */
static int open_dev(board_t *b)
{
    uint8_t mode = SPI_MODE_0, bits = 8;
    b->fd = open(b->dev, O_RDWR);
    if (b->fd < 0) { fprintf(stderr, "%s 열기 실패: %s\n", b->dev, strerror(errno)); return -1; }
    if (ioctl(b->fd, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(b->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(b->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0)
    {
        fprintf(stderr, "%s 설정 실패: %s\n", b->dev, strerror(errno));
        return -1;
    }
    return 0;
}

static int fail(const char *step, const char *why)
{
    printf("\n*** 실패: %s — %s\n", step, why);
    stop_both();
    printf("깨진 응답 수: X %u, Z %u\n", X.invalid, Z.invalid);
    return 1;
}

int main(int argc, char **argv)
{
    bool go_home = true;
    const char *e;
    int opt;
    while ((opt = getopt(argc, argv, "s:nv")) != -1)
    {
        if (opt == 's') speed_hz = (uint32_t)strtoul(optarg, NULL, 10);
        else if (opt == 'n') go_home = false;
        else if (opt == 'v') verbose = true;
        else { fprintf(stderr, "사용법: %s [-s HZ] [-n] [-v]\n", argv[0]); return 2; }
    }
    signal(SIGINT, on_signal);
    if (open_dev(&X) < 0 || open_dev(&Z) < 0) return 1;
    printf("START 시퀀스 시험 — SPI %u Hz\n", speed_hz);
    printf("확인: X 캐리지 A, 집게 열림, 두 보드 LED 빠른 깜빡임(백도어 빌드), 모터 전원 스위치 손 닿는 곳\n");

    /* 0) 양축 NOP로 상태와 ack_seq 읽기 → seq를 ack_seq+1부터 (규약 4장 재시작 절차) */
    printf("\n>>> 양축 NOP (seq 동기화)\n");
    X.hb_at = Z.hb_at = X.valid_at = Z.valid_at = now();
    for (int k = 0; k < 20 && !(X.have && Z.have); ++k)
    {
        (void)xfer(&X, CMD_NOP, 0, 0); sleep_ms(POLL_GAP_MS);
        (void)xfer(&Z, CMD_NOP, 0, 0); sleep_ms(POLL_GAP_MS);
    }
    if (!X.have || !Z.have) return fail("NOP", X.have ? "Z 응답 없음" : "X 응답 없음");
    X.seq = u16(X.last + 4);
    Z.seq = u16(Z.last + 4);
    X.hb_at = Z.hb_at = now();
    printf("  ack_seq: X=%u Z=%u, stage: X=%d Z=%d (15 = 부팅 후 ARM 전)\n", X.seq, Z.seq,
           (X.last[9] >> 3) & 15, (Z.last[9] >> 3) & 15);

    /* 1) STOP → ARM (양축) */
    if ((e = send_command(&X, CMD_STOP, 0))) return fail("X STOP", e);
    if ((e = send_command(&Z, CMD_STOP, 0))) return fail("Z STOP", e);
    if ((e = send_command(&X, CMD_ARM, 0))) return fail("X ARM", e);
    if ((e = send_command(&Z, CMD_ARM, 0))) return fail("Z ARM", e);
    for (int k = 0; k < 5; ++k) if ((e = poll_both())) return fail("ARM 확인", e);
    if (X.last[3] != ST_IDLE || Z.last[3] != ST_IDLE) return fail("ARM 확인", "state가 IDLE이 아님");

    /* 2) 복구: Z RECOVER → X RECOVER (반드시 Z 먼저) */
    if ((e = run_job(&Z, CMD_RECOVER, 0, MOVE_TIMEOUT_S))) return fail("Z RECOVER", e);
    if ((e = run_job(&X, CMD_RECOVER, 0, MOVE_TIMEOUT_S))) return fail("X RECOVER", e);

    /* 3) 반송: Z PICK → X_MOVE(1) → (E84 생략) → Z PLACE */
    if ((e = run_job(&Z, CMD_PICK, 0, PICK_TIMEOUT_S))) return fail("Z PICK", e);
    if ((e = run_job(&X, CMD_X_MOVE, 1, MOVE_TIMEOUT_S))) return fail("X X_MOVE(1)", e);
    printf("  (E84 확인 단계는 이 시험에서 생략)\n");
    if ((e = run_job(&Z, CMD_PLACE, 0, PICK_TIMEOUT_S))) return fail("Z PLACE", e);

    /* 4) 다음 전원 인가를 위해 X를 A로 복귀 */
    if (go_home && (e = run_job(&X, CMD_X_MOVE, 0, MOVE_TIMEOUT_S))) return fail("X A 복귀", e);

    printf("\n=== START 시퀀스 성공 ===  깨진 응답 수: X %u, Z %u\n", X.invalid, Z.invalid);
    close(X.fd);
    close(Z.fd);
    return 0;
}
