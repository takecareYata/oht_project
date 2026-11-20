/* 출력이 막혀도 진단 생산자가 기다리지 않는지 확인한다. SPI 장치는 열지 않는다. */
#include "../app/TSspi.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !TSspi_DEBUG_ENABLED
#error "This test exercises the enabled debug queue"
#endif
static pthread_mutex_t TSspi_test_write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t TSspi_test_write_ready = PTHREAD_COND_INITIALIZER;
static bool TSspi_test_block_output;
static bool TSspi_test_output_waiting;
static bool TSspi_test_write_error;
static bool TSspi_test_write_interrupt;
static char TSspi_test_output[TSspi_DEBUG_TEXT_SIZE];
static size_t TSspi_test_output_count;
static unsigned int TSspi_test_fault_count;

ssize_t __wrap_write(int fd, const void * buffer, size_t count);
ssize_t __wrap_write(int fd, const void * buffer, size_t count)
{
    const char * bytes = buffer;
    size_t amount = count > 13U ? 13U : count; /* 부분 쓰기도 처리해야 한다. */
    assert(fd == STDERR_FILENO);
    assert(pthread_mutex_lock(&TSspi_test_write_mutex) == 0);
    while (TSspi_test_block_output)
    {
        TSspi_test_output_waiting = true;
        assert(pthread_cond_signal(&TSspi_test_write_ready) == 0);
        assert(pthread_cond_wait(&TSspi_test_write_ready, &TSspi_test_write_mutex) == 0);
    }
    if (TSspi_test_write_error)
    {
        assert(pthread_mutex_unlock(&TSspi_test_write_mutex) == 0);
        errno = EIO;
        return -1;
    }
    if (TSspi_test_write_interrupt)
    {
        TSspi_test_write_interrupt = false;
        assert(pthread_mutex_unlock(&TSspi_test_write_mutex) == 0);
        errno = EINTR;
        return -1;
    }
    assert(TSspi_test_output_count + amount < sizeof(TSspi_test_output));
    memcpy(&TSspi_test_output[TSspi_test_output_count], bytes, amount);
    TSspi_test_output_count += amount;
    TSspi_test_output[TSspi_test_output_count] = '\0';
    assert(pthread_mutex_unlock(&TSspi_test_write_mutex) == 0);
    return (ssize_t)amount;
}
void TScontrol_Set_Fault(main_fault_t fault)
{
    (void)fault;
    ++TSspi_test_fault_count;
}
void TScontrol_Set_Axis_Event(main_axis_t axis, main_axis_event_t event, const main_axis_status_t * status)
{
    (void)axis;
    (void)event;
    (void)status;
}
static void * TSspi_test_consumer(void * argument)
{
    TSspi_debug_entry_t entry;
    (void)argument;
    assert(TSspi_Debug_Get(&entry));
    TSspi_Debug_Print(&entry);
    return NULL;
}
int main(void)
{
    TSspi_axis_context_t axis = { .axis=main_AXIS_X, .request={.cmd=2U,.seq=0x1234U}, .status={.state=main_AXIS_STATE_UNKNOWN,.ack_seq=0x1233U,.op_seq=0x1232U} };
    uint8_t bytes[24] = {0x5AU, 2U, 2U};
    TSspi_debug_entry_t entry;
    size_t index;
    pthread_t consumer;
    TSspi_debug_ready = true;
    TSspi_Debug_Event(&axis, "RX axis mismatch", 2U, bytes);
    bytes[0] = 0U;
    axis.request.seq = 0U;
    assert(TSspi_Debug_Get(&entry));
    assert(entry.seq == 0x1234U && entry.bytes[0] == 0x5AU);
    assert(entry.has_bytes && entry.ack_seq == 0x1233U);
    assert(TSspi_test_output_count == 0U); /* 생산 경로에는 출력 호출이 없다. */
    TSspi_test_write_interrupt = true;
    TSspi_Debug_Print(&entry);
    assert(strstr(TSspi_test_output, "TSspi X RX axis mismatch") != NULL);
    assert(strstr(TSspi_test_output, "seq=0x00001234") != NULL);
    assert(strstr(TSspi_test_output, "bytes=5A 02 02") != NULL);
    assert(TSspi_test_output[TSspi_test_output_count - 1U] == '\n');

    assert(pthread_mutex_lock(&TSspi_debug_mutex) == 0);
    TSspi_Debug_Event(&axis, "busy queue", 0U, NULL);
    assert(TSspi_debug_count == 0U);
    assert(pthread_mutex_unlock(&TSspi_debug_mutex) == 0);
    for (index = 0U; index < TSspi_DEBUG_QUEUE_SIZE + 5U; ++index)
    {
        TSspi_Debug_Event(&axis, "overflow", 0U, NULL);
    }
    assert(TSspi_debug_count == TSspi_DEBUG_QUEUE_SIZE && TSspi_debug_dropped == 5U);
    for (index = 0U; index < TSspi_DEBUG_QUEUE_SIZE; ++index)
    {
        assert(TSspi_Debug_Get(&entry));
    }
    assert(TSspi_Debug_Get(&entry) && entry.detail == 5U);
    assert(!TSspi_Debug_Get(&entry));

    TSspi_test_output_count = 0U;
    TSspi_Debug_Event(&axis, "slow output", 0U, NULL);
    TSspi_test_block_output = true;
    assert(pthread_create(&consumer, NULL, &TSspi_test_consumer, NULL) == 0);
    assert(pthread_mutex_lock(&TSspi_test_write_mutex) == 0);
    while (!TSspi_test_output_waiting)
    {
        assert(pthread_cond_wait(&TSspi_test_write_ready, &TSspi_test_write_mutex) == 0);
    }
    /* 소비자는 write에서 막혔지만 생산자는 큐를 계속 사용한다. */
    TSspi_Debug_Event(&axis, "producer while output blocked", 0U, NULL);
    assert(TSspi_Debug_Get(&entry));
    TSspi_test_block_output = false;
    assert(pthread_cond_signal(&TSspi_test_write_ready) == 0);
    assert(pthread_mutex_unlock(&TSspi_test_write_mutex) == 0);
    assert(pthread_join(consumer, NULL) == 0);
    TSspi_test_write_error = true;
    TSspi_Debug_Print(&entry);
    assert(TSspi_test_fault_count == 0U);
    puts("SPI debug: copied RX, formatting, partial/EINTR writes, busy/full queue, blocked output isolation PASS");
    return 0;
}
