#define _POSIX_C_SOURCE 200809L
#include "TSe84.h"
#include "TScontrol.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#if TSe84_DEBUG_ENABLED
#include <signal.h>
#endif

#define TSe84_SELECT_OUTPUTS (TSe84_SIGNAL_VALID_MASK | TSe84_SIGNAL_CS_0_MASK)
#define TSe84_REQUEST_OUTPUTS (TSe84_SELECT_OUTPUTS | TSe84_SIGNAL_TR_REQ_MASK)
#define TSe84_PERMISSION_INPUTS (TSe84_SIGNAL_HO_AVBL_MASK | TSe84_SIGNAL_ES_MASK)

/* gpiochip0 출력은 헤더 핀 29/31/32/33/35, 입력은 핀 36/37/38/40/15 순서다. */
static const uint32_t TSe84_output_offsets[TSe84_GPIO_LINE_COUNT] = { 105U, 106U, 41U, 43U, 53U };
static const uint32_t TSe84_input_offsets[TSe84_GPIO_LINE_COUNT] = { 113U, 124U, 52U, 51U, 85U };
static const uint32_t TSe84_input_masks[TSe84_GPIO_LINE_COUNT] =
{
    TSe84_SIGNAL_L_REQ_MASK, TSe84_SIGNAL_U_REQ_MASK, TSe84_SIGNAL_READY_MASK,
    TSe84_SIGNAL_HO_AVBL_MASK, TSe84_SIGNAL_ES_MASK
};

/* 중앙 FSM의 요청과 E84 폴링은 같은 mutex로 GPIO 및 상태를 보호한다. */
static pthread_mutex_t TSe84_mutex = PTHREAD_MUTEX_INITIALIZER;
static int TSe84_output_fd = -1;
static int TSe84_input_fd = -1;
static TSe84_config_t TSe84_config = { TSe84_TA1_MS, TSe84_TA2_MS, TSe84_TA3_MS, TSe84_TA4_MS };
static TSe84_signal_t TSe84_signals;
static TSe84_state_t TSe84_state = TSe84_STATE_IDLE;
static uint64_t TSe84_deadline_ms;

static bool TSe84_Init_Gpio(void);
static int TSe84_Get_Gpio_Handle(int chip_fd, uint32_t flags, const uint32_t * offsets);
static bool TSe84_Get_Input(void);
static bool TSe84_Set_Output(uint32_t output);
static bool TSe84_Get_Time(uint64_t * now);
static void TSe84_Set_State(TSe84_state_t state, uint64_t now, uint32_t timeout_ms);
static bool TSe84_Set_Reset(TSe84_state_t state);
static void TSe84_Set_Fault_Internal(main_fault_t fault);
static bool TSe84_Parse_Request(main_e84_request_t request);
static main_fault_t TSe84_Parse_Signal(uint64_t now);
static void TSe84_Set_Poll(void);

#if TSe84_DEBUG_ENABLED
static void TSe84_Debug_Init(void);
static void * TSe84_Debug_Task(void * argument);
static void TSe84_Debug_Event(const char * message, const char * detail_label, uint32_t detail);
static void TSe84_Debug_Input(int error);
static bool TSe84_Debug_Get(TSe84_debug_entry_t * entry);
static const char * TSe84_Debug_State(TSe84_state_t state);
static void TSe84_Debug_Print(const TSe84_debug_entry_t * entry);
static void TSe84_Debug_Text(char * text, size_t * length, const char * value);
static void TSe84_Debug_Number(char * text, size_t * length, uint32_t value);
static void TSe84_Debug_Write(const char * text, size_t length);
#else
#define TSe84_Debug_Init() ((void)0)
#define TSe84_Debug_Event(message, detail_label, detail) ((void)0)
#define TSe84_Debug_Input(error) ((void)0)
#endif

/* ==================== E84 태스크와 중앙 FSM 인터페이스 ==================== */
void * TSe84_Init_Task(void * argument)
{
    const struct timespec interval = { .tv_sec = 0, .tv_nsec = (long)(TSe84_POLL_MS * 1000000U) };
    (void)argument;
    TSe84_Debug_Init();
    TSe84_Init_Communication(NULL);

    for (;;)
    {
        if (pthread_mutex_lock(&TSe84_mutex) != 0)
        {
            TScontrol_Set_Fault(main_FAULT_INTERLOCK);
            break;
        }
        TSe84_Set_Poll();
        (void)pthread_mutex_unlock(&TSe84_mutex);

        if ((nanosleep(&interval, NULL) != 0) && (errno != EINTR))
        {
            TSe84_Set_Fault(main_FAULT_INTERLOCK);
            break;
        }
    }
    return NULL;
}

void TSe84_Init_Communication(const TSe84_config_t * config)
{
    if (pthread_mutex_lock(&TSe84_mutex) != 0)
    {
        return;
    }
    if (TSe84_state == TSe84_STATE_IDLE)
    {
        if ((config != NULL) && (config->l_req_timeout_ms > 0U) &&
            (config->ready_timeout_ms > 0U) && (config->transfer_timeout_ms > 0U) &&
            (config->complete_timeout_ms > 0U))
        {
            TSe84_config = *config;
        }
        (void)TSe84_Init_Gpio();
    }
    (void)pthread_mutex_unlock(&TSe84_mutex);
}

bool TSe84_Set_Request(main_e84_request_t request)
{
    bool accepted;
    if (pthread_mutex_lock(&TSe84_mutex) != 0)
    {
        return false;
    }
    accepted = TSe84_Parse_Request(request);
    if (accepted == false)
    {
        TSe84_Debug_Event("request rejected", " request=", (uint32_t)request);
        if (request != main_E84_REQUEST_NONE)
        {
            (void)TSe84_Set_Reset(TSe84_STATE_FAULT);
        }
    }
    (void)pthread_mutex_unlock(&TSe84_mutex);
    return accepted;
}

void TSe84_Get_Signal(TSe84_signal_t * signal)
{
    if (signal != NULL)
    {
        *signal = (TSe84_signal_t){ 0 };
        if (pthread_mutex_lock(&TSe84_mutex) == 0)
        {
            *signal = TSe84_signals;
            (void)pthread_mutex_unlock(&TSe84_mutex);
        }
    }
}

void TSe84_Set_Fault(main_fault_t fault)
{
    if (fault == main_FAULT_NONE)
    {
        return;
    }
    if (pthread_mutex_lock(&TSe84_mutex) == 0)
    {
        TSe84_Set_Fault_Internal(fault);
        (void)pthread_mutex_unlock(&TSe84_mutex);
    }
}

/* ==================== GPIO 입력과 출력 ==================== */
static bool TSe84_Init_Gpio(void)
{
    int chip_fd;
    if (TSe84_input_fd >= 0)
    {
        return true;
    }
    chip_fd = open(TSe84_GPIO_DEVICE, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0)
    {
        TSe84_Debug_Event("GPIO open failed " TSe84_GPIO_DEVICE, " errno=", (uint32_t)errno);
        return false;
    }

    /* libgpiod 1.6과 같은 GPIO character device v1 요청을 사용한다. */
    TSe84_output_fd = TSe84_Get_Gpio_Handle(chip_fd, GPIOHANDLE_REQUEST_OUTPUT, TSe84_output_offsets);
    if (TSe84_output_fd >= 0)
    {
        TSe84_signals.output_signals = TSe84_SIGNAL_NONE;
        TSe84_input_fd = TSe84_Get_Gpio_Handle(chip_fd, GPIOHANDLE_REQUEST_INPUT, TSe84_input_offsets);
        if (TSe84_input_fd < 0)
        {
            (void)close(TSe84_output_fd);
            TSe84_output_fd = -1;
        }
    }
    (void)close(chip_fd);
    if (TSe84_input_fd < 0)
    {
        return false;
    }
    TSe84_Debug_Event("GPIO ready " TSe84_GPIO_DEVICE, NULL, 0U);
    return true;
}

static int TSe84_Get_Gpio_Handle(int chip_fd, uint32_t flags, const uint32_t * offsets)
{
    struct gpiohandle_request request =
    {
        .flags = flags,
        .consumer_label = "TSe84",
        .lines = TSe84_GPIO_LINE_COUNT
    };
    size_t index;
    for (index = 0U; index < TSe84_GPIO_LINE_COUNT; ++index)
    {
        request.lineoffsets[index] = offsets[index];
    }
    /* 출력 핸들의 default_values는 전부 LOW이다. */
    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &request) != 0)
    {
        TSe84_Debug_Event((flags == GPIOHANDLE_REQUEST_OUTPUT) ? "GPIO output request failed" : "GPIO input request failed", " errno=", (uint32_t)errno);
        return -1;
    }
    return request.fd;
}

static bool TSe84_Get_Input(void)
{
    struct gpiohandle_data data = { 0 };
    size_t index;
    TSe84_signals.input_signals = TSe84_SIGNAL_NONE;
    if (TSe84_input_fd < 0)
    {
        return false;
    }
    if (ioctl(TSe84_input_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) != 0)
    {
        TSe84_Debug_Input(errno);
        return false;
    }
    for (index = 0U; index < TSe84_GPIO_LINE_COUNT; ++index)
    {
        if (data.values[index] != 0U)
        {
            TSe84_signals.input_signals |= TSe84_input_masks[index];
        }
    }
    TSe84_Debug_Input(0);
    return true;
}

static bool TSe84_Set_Output(uint32_t output)
{
    struct gpiohandle_data data = { 0 };
    size_t index;
#if TSe84_DEBUG_ENABLED
    const uint32_t previous = TSe84_signals.output_signals;
#endif
    for (index = 0U; index < TSe84_GPIO_LINE_COUNT; ++index)
    {
        data.values[index] = ((output & (UINT32_C(1) << index)) != 0U) ? 1U : 0U;
    }
    if (ioctl(TSe84_output_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) != 0)
    {
        TSe84_Debug_Event("GPIO write failed", " errno=", (uint32_t)errno);
        return false;
    }
    TSe84_signals.output_signals = output;
#if TSe84_DEBUG_ENABLED
    if (previous != output)
    {
        TSe84_Debug_Event("output changed", NULL, 0U);
    }
#endif
    return true;
}

/* ==================== 요청에 따른 E84 출력 전이 ==================== */
static bool TSe84_Parse_Request(main_e84_request_t request)
{
    const uint32_t ready_inputs = TSe84_PERMISSION_INPUTS | TSe84_SIGNAL_READY_MASK;
    uint64_t now = 0U;
    if (request != main_E84_REQUEST_NONE)
    {
        if (TSe84_Get_Time(&now) == false)
        {
            return false;
        }
    }

    switch (request)
    {
        case main_E84_REQUEST_NONE:
            TSe84_Debug_Event("request NONE (cancel)", NULL, 0U);
            return TSe84_Set_Reset(TSe84_STATE_IDLE);
        case main_E84_REQUEST_PREPARE:
            TSe84_Debug_Event("request PREPARE", NULL, 0U);
            if (TSe84_state != TSe84_STATE_IDLE)
            {
                return false;
            }
            if (TSe84_Init_Gpio() == false)
            {
                return false;
            }
            TSe84_Set_State(TSe84_STATE_PREPARE, now, TSe84_SETTLE_MS);
            return true;
        case main_E84_REQUEST_BUSY:
            TSe84_Debug_Event("request BUSY", NULL, 0U);
            if (TSe84_Get_Input() == false)
            {
                return false;
            }
            if ((TSe84_state != TSe84_STATE_READY) ||
                (TSe84_signals.input_signals != (ready_inputs | TSe84_SIGNAL_L_REQ_MASK)))
            {
                return false;
            }
            if (TSe84_Set_Output(TSe84_REQUEST_OUTPUTS | TSe84_SIGNAL_BUSY_MASK) == false)
            {
                return false;
            }
            TSe84_Set_State(TSe84_STATE_WAIT_L_REQ_OFF, now, TSe84_config.transfer_timeout_ms);
            return true;
        case main_E84_REQUEST_COMPLETE:
            TSe84_Debug_Event("request COMPLETE", NULL, 0U);
            if (TSe84_Get_Input() == false)
            {
                return false;
            }
            if ((TSe84_state != TSe84_STATE_WAIT_COMPLETE_REQUEST) ||
                (TSe84_signals.input_signals != ready_inputs))
            {
                return false;
            }
            /* PLACE 완료와 L_REQ OFF를 확인한 중앙 FSM이 요청한다. */
            if (TSe84_Set_Output(TSe84_SELECT_OUTPUTS) == false)
            {
                return false;
            }
            if (TSe84_Set_Output(TSe84_SELECT_OUTPUTS | TSe84_SIGNAL_COMPT_MASK) == false)
            {
                return false;
            }
            TSe84_Set_State(TSe84_STATE_WAIT_READY_OFF, now, TSe84_config.complete_timeout_ms);
            return true;
        default:
            return false;
    }
}

/* ==================== 입력에 따른 전이와 중앙 FSM 이벤트 ==================== */
static void TSe84_Set_Poll(void)
{
    const bool input_ok = TSe84_Get_Input();
    main_fault_t fault = main_FAULT_INTERLOCK;
    uint64_t now;
    bool time_ok;

    if ((TSe84_state == TSe84_STATE_IDLE) || (TSe84_state == TSe84_STATE_FAULT))
    {
        return;
    }
    time_ok = TSe84_Get_Time(&now);
    if ((input_ok == true) && (time_ok == true))
    {
        fault = TSe84_Parse_Signal(now);
    }
    if (fault != main_FAULT_NONE)
    {
        TSe84_Set_Fault_Internal(fault);
    }
}

static main_fault_t TSe84_Parse_Signal(uint64_t now)
{
    const uint32_t inputs = TSe84_signals.input_signals;
    const bool load_requested = (inputs & TSe84_SIGNAL_L_REQ_MASK) != 0U;
    const bool ready = (inputs & TSe84_SIGNAL_READY_MASK) != 0U;
    const bool settling = (TSe84_state == TSe84_STATE_PREPARE) || (TSe84_state == TSe84_STATE_COMPLETE);

    if ((settling == true) && (now < TSe84_deadline_ms))
    {
        if ((TSe84_state == TSe84_STATE_COMPLETE) && ((inputs & TSe84_SIGNAL_ES_MASK) == 0U))
        {
            return main_FAULT_INTERLOCK;
        }
        return main_FAULT_NONE;
    }
    if ((inputs & (TSe84_PERMISSION_INPUTS | TSe84_SIGNAL_U_REQ_MASK)) != TSe84_PERMISSION_INPUTS)
    {
        return main_FAULT_INTERLOCK;
    }
    if ((settling == false) && (TSe84_deadline_ms != 0U) && (now >= TSe84_deadline_ms))
    {
        return main_FAULT_E84_TIMEOUT;
    }

    switch (TSe84_state)
    {
        case TSe84_STATE_PREPARE:
            if ((load_requested == true) || (ready == true))
            {
                return main_FAULT_INTERLOCK;
            }
            if (TSe84_Set_Output(TSe84_SELECT_OUTPUTS) == false)
            {
                return main_FAULT_INTERLOCK;
            }
            TSe84_Set_State(TSe84_STATE_WAIT_L_REQ_ON, now, TSe84_config.l_req_timeout_ms);
            break;
        case TSe84_STATE_WAIT_L_REQ_ON:
            if (ready == true)
            {
                return main_FAULT_INTERLOCK;
            }
            if (load_requested == true)
            {
                if (TSe84_Set_Output(TSe84_REQUEST_OUTPUTS) == false)
                {
                    return main_FAULT_INTERLOCK;
                }
                TSe84_Set_State(TSe84_STATE_WAIT_READY_ON, now, TSe84_config.ready_timeout_ms);
            }
            break;
        case TSe84_STATE_WAIT_READY_ON:
            if (load_requested == false)
            {
                return main_FAULT_INTERLOCK;
            }
            if (ready == true)
            {
                TSe84_Set_State(TSe84_STATE_READY, now, 0U);
                TSe84_Debug_Event("event READY", NULL, 0U);
                TScontrol_Set_E84_Event(main_E84_EVENT_READY);
            }
            break;
        case TSe84_STATE_READY:
            if ((load_requested == false) || (ready == false))
            {
                return main_FAULT_INTERLOCK;
            }
            break;
        case TSe84_STATE_WAIT_L_REQ_OFF:
            if (ready == false)
            {
                return main_FAULT_INTERLOCK;
            }
            if (load_requested == false)
            {
                TSe84_Set_State(TSe84_STATE_WAIT_COMPLETE_REQUEST, now, 0U);
                TSe84_Debug_Event("event LOAD_REQUEST_OFF", NULL, 0U);
                TScontrol_Set_E84_Event(main_E84_EVENT_LOAD_REQUEST_OFF);
            }
            break;
        case TSe84_STATE_WAIT_COMPLETE_REQUEST:
            if ((load_requested == true) || (ready == false))
            {
                return main_FAULT_INTERLOCK;
            }
            break;
        case TSe84_STATE_WAIT_READY_OFF:
            if (load_requested == true)
            {
                return main_FAULT_INTERLOCK;
            }
            if (ready == false)
            {
                if (TSe84_Set_Output(TSe84_SIGNAL_NONE) == false)
                {
                    return main_FAULT_INTERLOCK;
                }
                TSe84_Set_State(TSe84_STATE_COMPLETE, now, TSe84_SETTLE_MS);
            }
            break;
        case TSe84_STATE_COMPLETE:
            if ((load_requested == true) || (ready == true))
            {
                return main_FAULT_INTERLOCK;
            }
            TSe84_Set_State(TSe84_STATE_IDLE, now, 0U);
            TSe84_Debug_Event("event COMPLETE", NULL, 0U);
            TScontrol_Set_E84_Event(main_E84_EVENT_COMPLETE);
            break;
        case TSe84_STATE_IDLE:
        case TSe84_STATE_FAULT:
            break;
        default:
            return main_FAULT_INTERLOCK;
    }
    return main_FAULT_NONE;
}

/* ==================== 제한 시간과 고장 처리 ==================== */
static bool TSe84_Get_Time(uint64_t * now)
{
    struct timespec stamp;
    if (clock_gettime(CLOCK_MONOTONIC, &stamp) != 0)
    {
        TSe84_Debug_Event("clock_gettime failed", " errno=", (uint32_t)errno);
        return false;
    }
    if (stamp.tv_sec < 0)
    {
        return false;
    }
    *now = ((uint64_t)stamp.tv_sec * 1000U) + ((uint64_t)stamp.tv_nsec / 1000000U);
    return true;
}

static void TSe84_Set_State(TSe84_state_t state, uint64_t now, uint32_t timeout_ms)
{
#if TSe84_DEBUG_ENABLED
    const TSe84_state_t previous = TSe84_state;
#endif
    TSe84_state = state;
    TSe84_deadline_ms = (timeout_ms == 0U) ? 0U : (now + (uint64_t)timeout_ms);
#if TSe84_DEBUG_ENABLED
    if (previous != state)
    {
        TSe84_Debug_Event("state entered", " timeout_ms=", timeout_ms);
    }
#endif
}

static bool TSe84_Set_Reset(TSe84_state_t state)
{
    bool outputs_off = true;
    if (TSe84_output_fd >= 0)
    {
        outputs_off = TSe84_Set_Output(TSe84_SIGNAL_NONE);
    }
    TSe84_Set_State(outputs_off ? state : TSe84_STATE_FAULT, 0U, 0U);
    return outputs_off;
}

static void TSe84_Set_Fault_Internal(main_fault_t fault)
{
    if (TSe84_state != TSe84_STATE_FAULT)
    {
        TSe84_Debug_Event((fault == main_FAULT_E84_TIMEOUT) ? "handshake timeout" : "fault", " fault=", (uint32_t)fault);
        (void)TSe84_Set_Reset(TSe84_STATE_FAULT);
        TScontrol_Set_Fault(fault);
    }
}

/* ==================== 디버그 출력 ==================== */
#if TSe84_DEBUG_ENABLED
static pthread_mutex_t TSe84_debug_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t TSe84_debug_available = PTHREAD_COND_INITIALIZER;
static TSe84_debug_entry_t TSe84_debug_entries[TSe84_DEBUG_QUEUE_SIZE];
static size_t TSe84_debug_head;
static size_t TSe84_debug_count;
static uint32_t TSe84_debug_last_input;
static int TSe84_debug_last_input_error = -1;

static void TSe84_Debug_Init(void)
{
    pthread_attr_t attributes;
    pthread_t thread;
    if (pthread_attr_init(&attributes) != 0)
    {
        return;
    }
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) == 0)
    {
        (void)pthread_create(&thread, &attributes, &TSe84_Debug_Task, NULL);
    }
    (void)pthread_attr_destroy(&attributes);
}

/* 제어 호출은 TSe84_mutex를 보유한 상태에서 신호를 복사한다. */
static void TSe84_Debug_Event(const char * message, const char * detail_label, uint32_t detail)
{
    if (pthread_mutex_trylock(&TSe84_debug_mutex) != 0)
    {
        return;
    }
    if (TSe84_debug_count < TSe84_DEBUG_QUEUE_SIZE)
    {
        const size_t index = (TSe84_debug_head + TSe84_debug_count) % TSe84_DEBUG_QUEUE_SIZE;
        TSe84_debug_entries[index] = (TSe84_debug_entry_t){
            .message = message,
            .detail_label = detail_label,
            .detail = detail,
            .state = TSe84_state,
            .signals = TSe84_signals,
            .inputs_valid = (TSe84_debug_last_input_error == 0),
            .outputs_valid = (TSe84_output_fd >= 0)
        };
        ++TSe84_debug_count;
        (void)pthread_cond_signal(&TSe84_debug_available);
    }
    (void)pthread_mutex_unlock(&TSe84_debug_mutex);
}

static void TSe84_Debug_Input(int error)
{
    if ((error == TSe84_debug_last_input_error) &&
        (TSe84_signals.input_signals == TSe84_debug_last_input))
    {
        return;
    }
    TSe84_debug_last_input = TSe84_signals.input_signals;
    TSe84_debug_last_input_error = error;
    if (error == 0)
    {
        TSe84_Debug_Event("input sample", NULL, 0U);
    }
    else
    {
        TSe84_Debug_Event("GPIO read failed", " errno=", (uint32_t)error);
    }
}

static void * TSe84_Debug_Task(void * argument)
{
    TSe84_debug_entry_t entry;
    sigset_t signals;
    (void)argument;
    /* stderr 파이프 오류가 제어 프로세스를 종료하지 않도록 처리한다. */
    if (sigemptyset(&signals) != 0)
    {
        return NULL;
    }
    if (sigaddset(&signals, SIGPIPE) != 0)
    {
        return NULL;
    }
    if (pthread_sigmask(SIG_BLOCK, &signals, NULL) != 0)
    {
        return NULL;
    }
    while (TSe84_Debug_Get(&entry) == true)
    {
        TSe84_Debug_Print(&entry);
    }
    return NULL;
}

static bool TSe84_Debug_Get(TSe84_debug_entry_t * entry)
{
    int result = pthread_mutex_lock(&TSe84_debug_mutex);
    if (result != 0)
    {
        return false;
    }
    while ((TSe84_debug_count == 0U) && (result == 0))
    {
        result = pthread_cond_wait(&TSe84_debug_available, &TSe84_debug_mutex);
    }
    if (result == 0)
    {
        *entry = TSe84_debug_entries[TSe84_debug_head];
        TSe84_debug_head = (TSe84_debug_head + 1U) % TSe84_DEBUG_QUEUE_SIZE;
        --TSe84_debug_count;
    }
    (void)pthread_mutex_unlock(&TSe84_debug_mutex);
    return result == 0;
}

static const char * TSe84_Debug_State(TSe84_state_t state)
{
    static const char * const names[] =
    {
        [TSe84_STATE_IDLE] = "IDLE",
        [TSe84_STATE_PREPARE] = "PREPARE",
        [TSe84_STATE_WAIT_L_REQ_ON] = "WAIT_L_REQ_ON",
        [TSe84_STATE_WAIT_READY_ON] = "WAIT_READY_ON",
        [TSe84_STATE_READY] = "READY",
        [TSe84_STATE_WAIT_L_REQ_OFF] = "WAIT_L_REQ_OFF",
        [TSe84_STATE_WAIT_COMPLETE_REQUEST] = "WAIT_COMPLETE_REQUEST",
        [TSe84_STATE_WAIT_READY_OFF] = "WAIT_READY_OFF",
        [TSe84_STATE_COMPLETE] = "COMPLETE",
        [TSe84_STATE_FAULT] = "FAULT"
    };
    const size_t index = (size_t)state;
    if (index >= (sizeof(names) / sizeof(names[0])))
    {
        return "UNKNOWN";
    }
    return names[index];
}

static void TSe84_Debug_Print(const TSe84_debug_entry_t * entry)
{
    static const char * const output_names[TSe84_GPIO_LINE_COUNT] =
    {
        " VALID=", " CS_0=", " TR_REQ=", " BUSY=", " COMPT="
    };
    static const char * const input_names[TSe84_GPIO_LINE_COUNT] =
    {
        " L_REQ=", " U_REQ=", " READY=", " HO_AVBL=", " ES="
    };
    char text[TSe84_DEBUG_TEXT_SIZE];
    size_t length = 0U;
    size_t index;

    TSe84_Debug_Text(text, &length, "TSe84 ");
    TSe84_Debug_Text(text, &length, entry->message);
    TSe84_Debug_Text(text, &length, " state=");
    TSe84_Debug_Text(text, &length, TSe84_Debug_State(entry->state));
    if (entry->detail_label != NULL)
    {
        TSe84_Debug_Text(text, &length, entry->detail_label);
        TSe84_Debug_Number(text, &length, entry->detail);
    }
    TSe84_Debug_Text(text, &length, " | OUT");
    if (entry->outputs_valid == true)
    {
        for (index = 0U; index < TSe84_GPIO_LINE_COUNT; ++index)
        {
            const uint32_t mask = UINT32_C(1) << index;
            TSe84_Debug_Text(text, &length, output_names[index]);
            TSe84_Debug_Text(text, &length, ((entry->signals.output_signals & mask) != 0U) ? "1" : "0");
        }
    }
    else
    {
        TSe84_Debug_Text(text, &length, " unavailable");
    }
    TSe84_Debug_Text(text, &length, " | IN");
    if (entry->inputs_valid == true)
    {
        for (index = 0U; index < TSe84_GPIO_LINE_COUNT; ++index)
        {
            TSe84_Debug_Text(text, &length, input_names[index]);
            TSe84_Debug_Text(text, &length, ((entry->signals.input_signals & TSe84_input_masks[index]) != 0U) ? "1" : "0");
        }
    }
    else
    {
        TSe84_Debug_Text(text, &length, " unavailable");
    }
    text[length] = '\n';
    TSe84_Debug_Write(text, length + 1U);
}

static void TSe84_Debug_Text(char * text, size_t * length, const char * value)
{
    size_t index = 0U;
    while ((*length < (TSe84_DEBUG_TEXT_SIZE - 1U)) && (value[index] != '\0'))
    {
        text[*length] = value[index];
        ++(*length);
        ++index;
    }
}

static void TSe84_Debug_Number(char * text, size_t * length, uint32_t value)
{
    const char digits[] = "0123456789";
    char number[11] = { '\0' };
    size_t index = sizeof(number) - 1U;
    uint32_t remaining = value;
    do
    {
        --index;
        number[index] = digits[remaining % 10U];
        remaining /= 10U;
    } while (remaining != 0U);
    TSe84_Debug_Text(text, length, &number[index]);
}

static void TSe84_Debug_Write(const char * text, size_t length)
{
    size_t position = 0U;
    while (position < length)
    {
        const ssize_t count = write(STDERR_FILENO, &text[position], length - position);
        if (count > 0)
        {
            position += (size_t)count;
        }
        else if ((count == 0) || (errno != EINTR))
        {
            break;
        }
        else
        {
            /* 시그널로 중단된 쓰기를 재시도한다. */
        }
    }
}
#endif /* TSe84_DEBUG_ENABLED */
