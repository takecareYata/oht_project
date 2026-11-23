#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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

/* X + Z bench sequencer WITHOUT SENSORS, protocol v2 (24-byte frames).
 *   X board = STM32 board 2, wire axis 1, CS0 (/dev/spidev0.0)
 *   Z board = STM32 board 1, wire axis 2, CS1 (/dev/spidev0.1)
 * X: X_MOVE 0/1 is a step-count move in the default sensorless firmware
 *    (CONFIG_X_HOME_MODE 0, carriage at A at power-on) and ends in DONE by itself.
 * Z: the firmware moves Z until a sensor, so Z is timed from here:
 *    down = PICK (first stage drives Z down), up = RECOVER, STOP after N ms.
 *    The gripper does not move (PICK reaches it only after the bottom sensor).
 * Every move is ARM -> command -> (wait) -> STOP, because STOP disarms.
 * Both boards are polled continuously so each keeps its 500 ms link alive.
 * Remote interlock flags are FAKED (no sensors): X gets Z_UP, Z gets
 * X_STOPPED|X_STATION. This program itself only runs one axis at a time.
 * BENCH TEST ONLY. Keep the motor power switch in reach. */
#define FRAME 24
#define CMD_POLL 0
#define CMD_ARM 1
#define CMD_STOP 2
#define CMD_RECOVER 3
#define CMD_X_MOVE 16
#define CMD_PICK 36
#define ST_STOPPED 0
#define ST_IDLE 1
#define ST_BUSY 2
#define ST_DONE 3
#define ST_ERROR 4
#define POLL_MS 25          /* per board, boards alternate: each polled every ~50 ms */
#define MOVE_MAX_MS 14000   /* firmware faults a move with TIMEOUT after 15 s */
#define MAX_STEPS 64

typedef struct {
    const char *name;
    int fd;
    uint8_t axis;
    uint8_t remote;
    uint16_t seq;
    unsigned valid, invalid;
    double valid_at;
    bool have;              /* last[] holds a valid response */
    uint8_t last[FRAME];
    uint8_t shown_state, shown_stage;
} board_t;

static volatile sig_atomic_t interrupted;
static uint32_t speed = 100000;
static bool raw, blind;
static board_t X = { "X", -1, 1, 1 /* Z_UP */, 0, 0, 0, 0, false, {0}, 99, 99 };
static board_t Z = { "Z", -1, 2, 6 /* X_STOPPED|X_STATION */, 0, 0, 0, 0, false, {0}, 99, 99 };

static void on_signal(int sig) { (void)sig; interrupted = 1; }
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static void sleep_ms(unsigned ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (unsigned j = 0; j < 8; ++j)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
static uint16_t next_seq(uint16_t s) { return s == UINT16_MAX ? 1 : (uint16_t)(s + 1); }
static const char *state_name(uint8_t s) {
    static const char *n[] = {"STOPPED", "IDLE", "BUSY", "DONE", "ERROR"};
    return s <= 4 ? n[s] : "?";
}
static const char *error_name(uint8_t e) {
    static const char *n[] = {"OK", "STOP", "LINK", "SENSOR", "TIMEOUT", "INTERLOCK",
                              "GRIP", "BUSY", "COMMAND", "SEQUENCE", "TRANSPORT"};
    return e <= 10 ? n[e] : "?";
}
static const char *cmd_name(uint8_t c) {
    switch (c) {
    case CMD_POLL: return "NOP"; case CMD_ARM: return "ARM"; case CMD_STOP: return "STOP";
    case CMD_RECOVER: return "RECOVER"; case CMD_X_MOVE: return "X_MOVE"; case CMD_PICK: return "PICK";
    default: return "?";
    }
}
static void hex(const uint8_t *p) {
    for (unsigned i = 0; i < FRAME; ++i) printf("%02X%s", p[i], i == FRAME-1 ? "\n" : " ");
}
/* The STM32 enables its shared MISO from the NSS falling-edge interrupt, so
 * the first response bytes can be lost. They are the constant header
 * 5A 02 axis: restore them, then the CRC decides. A wrong board or later
 * damage still fails the CRC (reported as CRC, not as axis mismatch). */
static void restore_header(uint8_t *p, uint8_t axis) {
    p[0] = 0x5a; p[1] = 2; p[2] = axis;
}
static const char *validate(const uint8_t *p, uint8_t axis) {
    if (p[0] != 0x5a) return "magic";
    if (p[1] != 2) return "version";
    if (p[2] != axis) return "axis / CS mismatch";
    if (p[17] || p[20] || p[21]) return "reserved bytes";
    if (u16(p + 22) != crc16(p, 22)) return "CRC";
    if (p[3] > 4 || p[6] > 10 || p[7] > 10 || p[16] > 3) return "undefined field value";
    return NULL;
}
static void show(const board_t *b, const uint8_t *p) {
    uint32_t bits = ((uint32_t)p[12] << 24) | ((uint32_t)p[13] << 16) | ((uint32_t)p[14] << 8) | p[15];
    int64_t pos = (bits & UINT32_C(0x80000000)) ? (int64_t)bits - INT64_C(4294967296) : bits;
    printf("  [%s] state=%s ack=%u receipt=%s fault=%s op=%u stage=%u link=%u pos=%" PRId64 " hb=%u\n",
           b->name, state_name(p[3]), u16(p+4), error_name(p[6]), error_name(p[7]), u16(p+10),
           (p[9]>>3)&15, p[9]>>7, pos, u16(p+18));
}
/* One 24-byte transaction. Returns 1 valid, 0 invalid response, -1 SPI error. */
static int xfer(board_t *b, uint8_t cmd, uint16_t seq, uint8_t arg) {
    uint8_t tx[FRAME] = {0}, rx[FRAME];
    tx[0] = 0xa5; tx[1] = 2; tx[2] = b->axis; tx[3] = cmd;
    put16(tx + 4, seq); tx[6] = arg; tx[7] = b->remote;
    put16(tx + 22, crc16(tx, 22));
    struct spi_ioc_transfer tr = {0};
    tr.tx_buf = (uintptr_t)tx; tr.rx_buf = (uintptr_t)rx;
    tr.len = FRAME; tr.speed_hz = speed; tr.bits_per_word = 8;
    int rc = ioctl(b->fd, SPI_IOC_MESSAGE(1), &tr);
    if (rc != FRAME) { fprintf(stderr, "[%s] SPI transfer failed (%d)\n", b->name, rc); return -1; }
    if (raw) { printf("  [%s] TX: ", b->name); hex(tx); printf("  [%s] RX: ", b->name); hex(rx); }
    restore_header(rx, b->axis);
    const char *error = validate(rx, b->axis);
    if (error) {
        ++b->invalid;
        if (!blind) { printf("  [%s] INVALID (%s): ", b->name, error); hex(rx); }
        return 0;
    }
    ++b->valid; b->valid_at = now(); b->have = true;
    memcpy(b->last, rx, FRAME);
    uint8_t stage = (uint8_t)((rx[9] >> 3) & 15);
    if (rx[3] != b->shown_state || stage != b->shown_stage) {
        b->shown_state = rx[3]; b->shown_stage = stage; show(b, rx);
    }
    return 1;
}
/* Poll both boards once (NOP). Keeps both links alive. */
static int tick(void) {
    if (xfer(&X, CMD_POLL, 0, 0) < 0) return -1;
    sleep_ms(POLL_MS);
    if (xfer(&Z, CMD_POLL, 0, 0) < 0) return -1;
    sleep_ms(POLL_MS);
    return 0;
}
/* Send a command and wait for its ack. Blind mode (MISO not readable):
 * the same frame is sent 3 times (a repeated seq is idempotent) and the
 * result is not confirmed. Returns NULL on acceptance. */
static const char *send_cmd(board_t *b, uint8_t cmd, uint8_t arg) {
    b->seq = next_seq(b->seq);
    printf(">>> [%s] %s arg=%u seq=%u\n", b->name, cmd_name(cmd), arg, b->seq);
    if (blind) {
        for (int i = 0; i < 3; ++i) {
            if (xfer(b, cmd, b->seq, arg) < 0) return "SPI error";
            if (tick()) return "SPI error";
        }
        return NULL;
    }
    if (xfer(b, cmd, b->seq, arg) < 0) return "SPI error";
    double start = now();
    while (now() - start < 1.0) {
        if (tick()) return "SPI error";
        if (b->have && u16(b->last + 4) == b->seq) {
            if (b->last[6] != 0) {
                printf("<<< [%s] %s rejected: %s\n", b->name, cmd_name(cmd), error_name(b->last[6]));
                return "command rejected";
            }
            return NULL;
        }
    }
    return "no ack within 1000 ms (check MISO, or use --blind)";
}
static bool stop_board(board_t *b) {
    const char *e = send_cmd(b, CMD_STOP, 0);
    if (e) { fprintf(stderr, "[%s] STOP not confirmed: %s\n", b->name, e); return false; }
    return true;
}
/* Wait up to ms while polling. done_seq != 0: return early when board b
 * reports DONE for that op. Returns NULL on time-out/DONE, else an error. */
static const char *watch(board_t *b, unsigned ms, uint16_t done_seq, bool *done) {
    double start = now();
    *done = false;
    while (now() - start < ms / 1000.0) {
        if (interrupted) return "interrupted (Ctrl+C)";
        if (tick()) return "SPI error";
        if (blind || !b->have) continue;
        if (now() - b->valid_at >= 0.4) return "no valid response for 400 ms";
        if (b->last[3] == ST_ERROR) return error_name(b->last[7]);
        if (b->last[3] == ST_STOPPED) return "board stopped unexpectedly";
        if (done_seq && b->last[3] == ST_DONE && u16(b->last + 10) == done_seq) { *done = true; return NULL; }
    }
    return NULL;
}
/* Pause between moves: keep polling both boards (links stay alive) but do
 * not judge their state: each board is STOPPED by our own STOP here. */
static const char *pause_ms(unsigned ms) {
    double start = now();
    while (now() - start < ms / 1000.0) {
        if (interrupted) return "interrupted (Ctrl+C)";
        if (tick()) return "SPI error";
    }
    return NULL;
}
/* ARM -> cmd -> watch -> STOP. timed: STOP after ms. Otherwise wait for DONE
 * (ms is the safety limit). */
static const char *move(board_t *b, uint8_t cmd, uint8_t arg, unsigned ms, bool timed, const char *what) {
    const char *e;
    bool done = false;
    printf("=== %s (%s, %s %u ms)\n", what, b->name, timed ? "timed" : "until DONE, max", ms);
    if ((e = send_cmd(b, CMD_ARM, 0)) != NULL) return e;
    e = send_cmd(b, cmd, arg);
    if (!e) e = watch(b, ms, timed ? 0 : b->seq, &done);
    if (!e && !timed && !done && !blind) e = "no DONE within the limit";
    bool stopped = stop_board(b);
    if (!e && !stopped) e = "STOP not confirmed";
    if (!e) printf("--- %s finished%s\n", what, timed ? "" : (done ? " (DONE)" : " (blind: time limit)"));
    return e;
}

typedef enum { ST_ZDOWN, ST_ZUP, ST_XA, ST_XB, ST_WAIT } step_t;
static int parse_steps(const char *s, step_t *steps) {
    char buf[512];
    int n = 0;
    snprintf(buf, sizeof buf, "%s", s);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        if (n >= MAX_STEPS) return -1;
        if (!strcmp(t, "zdown")) steps[n++] = ST_ZDOWN;
        else if (!strcmp(t, "zup")) steps[n++] = ST_ZUP;
        else if (!strcmp(t, "xa")) steps[n++] = ST_XA;
        else if (!strcmp(t, "xb")) steps[n++] = ST_XB;
        else if (!strcmp(t, "wait")) steps[n++] = ST_WAIT;
        else { fprintf(stderr, "Unknown step: %s\n", t); return -1; }
    }
    return n;
}
static unsigned number(const char *s, unsigned min, unsigned max) {
    char *end; errno = 0;
    unsigned long v = strtoul(s, &end, 0);
    if (errno || !*s || *end || v < min || v > max) { fprintf(stderr, "Invalid number: %s\n", s); exit(2); }
    return (unsigned)v;
}
static void usage(const char *name) {
    printf("Usage: %s [options]\n"
           "X + Z bench sequence WITHOUT SENSORS. Default steps: zdown,zup,xb,zdown,zup,xa\n"
           "  --steps LIST       comma list of: zdown zup xa xb wait\n"
           "  --zdown-ms N       Z down time (default 2000, max %u)\n"
           "  --zup-ms N         Z up time (default = zdown-ms)\n"
           "  --x-ms N           X move limit; X stops itself at A/B (default 14000)\n"
           "  --wait-ms N        pause for 'wait' and between steps (default 500)\n"
           "  --cycles N         repeat the step list (default 1)\n"
           "  --speed HZ         SPI clock (default 100000)\n"
           "  --x-dev PATH       default /dev/spidev0.0 (CS0)\n"
           "  --z-dev PATH       default /dev/spidev0.1 (CS1)\n"
           "  --blind            do not require valid responses (e.g. MISO not connected);\n"
           "                     commands sent 3x, acks/DONE not checked, X waits --x-ms\n"
           "  --raw              print every TX/RX frame\n"
           "Power-on assumption (sensorless X): carriage at A. Z: start at the top.\n"
           "Remote interlock flags are faked. BENCH TEST ONLY.\n", name, MOVE_MAX_MS);
}
static int open_spi(const char *dev) {
    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror(dev); return -1; }
    uint32_t mode = SPI_MODE_0;
    uint8_t bpw = 8, lsb = 0;
    if (ioctl(fd, SPI_IOC_WR_MODE32, &mode) < 0 || ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0
        || ioctl(fd, SPI_IOC_WR_LSB_FIRST, &lsb) < 0 || ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI configuration"); close(fd); return -1;
    }
    return fd;
}
/* Start-up: read each board's ack_seq if possible, then STOP it. A valid STOP
 * is accepted with any seq and resets the board's sequence reference, so the
 * following commands are accepted even when the old seq is unknown (blind). */
static const char *start_board(board_t *b) {
    b->seq = 1000;
    for (int i = 0; i < 6; ++i) {
        if (xfer(b, CMD_POLL, 0, 0) < 0) return "SPI error";
        if (b->have) b->seq = u16(b->last + 4);
        sleep_ms(POLL_MS);
    }
    if (!blind && !b->have) return "no valid response (check wiring/MISO, or use --blind)";
    return stop_board(b) || blind ? NULL : "STOP not confirmed";
}
int main(int argc, char **argv) {
    const char *xdev = "/dev/spidev0.0", *zdev = "/dev/spidev0.1";
    const char *list = "zdown,zup,xb,zdown,zup,xa";
    unsigned zdown = 2000, zup = 0, xms = MOVE_MAX_MS, wait_ms = 500, cycles = 1;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--help")) { usage(argv[0]); return 0; }
        if (!strcmp(a, "--blind")) { blind = true; continue; }
        if (!strcmp(a, "--raw")) { raw = true; continue; }
        if (i + 1 >= argc) { usage(argv[0]); return 2; }
        const char *v = argv[++i];
        if (!strcmp(a, "--steps")) list = v;
        else if (!strcmp(a, "--zdown-ms")) zdown = number(v, 100, MOVE_MAX_MS);
        else if (!strcmp(a, "--zup-ms")) zup = number(v, 100, MOVE_MAX_MS);
        else if (!strcmp(a, "--x-ms")) xms = number(v, 500, MOVE_MAX_MS);
        else if (!strcmp(a, "--wait-ms")) wait_ms = number(v, 0, 60000);
        else if (!strcmp(a, "--cycles")) cycles = number(v, 1, 1000);
        else if (!strcmp(a, "--speed")) speed = number(v, 1000, 10000000);
        else if (!strcmp(a, "--x-dev")) xdev = v;
        else if (!strcmp(a, "--z-dev")) zdev = v;
        else { usage(argv[0]); return 2; }
    }
    if (!zup) zup = zdown;
    step_t steps[MAX_STEPS];
    int count = parse_steps(list, steps);
    if (count <= 0) { usage(argv[0]); return 2; }

    if ((X.fd = open_spi(xdev)) < 0 || (Z.fd = open_spi(zdev)) < 0) return 1;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("X=%s Z=%s mode=0 speed=%u steps=%s cycles=%u zdown=%u zup=%u x-limit=%u%s\n",
           xdev, zdev, speed, list, cycles, zdown, zup, xms, blind ? " BLIND" : "");
    puts("WARNING: no sensors. Z timed moves do NOT stop at the ends; interlocks are faked.");
    puts("         X must start at A, Z at the top. Keep the motor power switch in reach.");

    const char *e = start_board(&X);
    if (e) printf("[X] start failed: %s\n", e);
    if (!e && (e = start_board(&Z)) != NULL) printf("[Z] start failed: %s\n", e);

    unsigned done = 0, total = (unsigned)count * cycles;
    for (unsigned c = 0; !e && c < cycles; ++c) {
        for (int s = 0; !e && s < count; ++s) {
            printf("\n##### cycle %u/%u step %d/%d\n", c + 1, cycles, s + 1, count);
            switch (steps[s]) {
            case ST_ZDOWN: e = move(&Z, CMD_PICK, 0, zdown, true, "Z down"); break;
            case ST_ZUP:   e = move(&Z, CMD_RECOVER, 0, zup, true, "Z up"); break;
            case ST_XA:    e = move(&X, CMD_X_MOVE, 0, xms, false, "X -> A"); break;
            case ST_XB:    e = move(&X, CMD_X_MOVE, 1, xms, false, "X -> B"); break;
            case ST_WAIT:
                printf("=== wait %u ms\n", wait_ms);
                e = pause_ms(wait_ms);
                break;
            }
            if (!e) {
                ++done;
                if (steps[s] != ST_WAIT && wait_ms) e = pause_ms(wait_ms);
            }
        }
    }
    if (e) {
        printf("\n!!! FAILED: %s -> STOP both boards\n", e);
        interrupted = 0;            /* let the STOP acks be awaited */
        stop_board(&X);
        stop_board(&Z);
    }
    printf("\nsteps done: %u/%u  X valid=%u invalid=%u  Z valid=%u invalid=%u\n",
           done, total, X.valid, X.invalid, Z.valid, Z.invalid);
    puts(blind ? "BLIND mode: nothing was confirmed by the boards; check motion by eye."
               : "Check motion by eye: X DONE = step count reached, Z = time elapsed.");
    printf("%s\n", !e && done == total ? "PASS" : "FAIL/INCOMPLETE");
    close(X.fd); close(Z.fd);
    return !e && done == total ? 0 : 1;
}
