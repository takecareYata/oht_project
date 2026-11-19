#include "main.h"
#include "TScontrol.h"
#include "TSspi.h"
#include "TSe84.h"
#include "TStcp.h"

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>

/* 다른 번역 단위에서 호출하지 않는 메인 태스크는 내부 연결로 제한한다. */
static void * main_Init_Task(void * argument);

int main(void)
{
    pthread_t main_id;
    /* join 실패 시에도 작업 스레드에 전달한 객체의 수명이 끝나지 않는다. */
    static main_task_result_t task_result = main_TASK_RESULT_FAILURE;
    int result = EXIT_FAILURE;
    int ret;

    /* DEV-PTHREAD-01/DEV-VOID-01: 정적 수명의 결과 객체만 전달한다. */
    ret = pthread_create(&main_id, NULL, &main_Init_Task, (void *)&task_result);
    if (ret == 0)
    {
        ret = pthread_join(main_id, NULL);
        if (ret == 0)
        {
            if (task_result == main_TASK_RESULT_SUCCESS)
            {
                result = EXIT_SUCCESS;
            }
        }
    }

    return result;
}

static void * main_Init_Task(void * argument)
{
    if (argument != NULL)
    {
        const main_task_entry_t task_entries[main_TASK_COUNT] =
        {
            &TScontrol_Init_Task,
            &TSspi_Init_Task,
            &TSe84_Init_Task,
            &TStcp_Init_Task
        };
        /* DEV-VOID-01: main()에서 전달한 원래 객체 형식으로만 복원한다. */
        main_task_result_t * const task_result = (main_task_result_t *)argument;
        pthread_t task_ids[main_TASK_COUNT];
        size_t index;
        size_t created_count = 0U;
        main_task_result_t result = main_TASK_RESULT_SUCCESS;
        int ret;

        /* 생성 순서는 실제 실행 순서를 보장하지 않는다. */
        for (index = 0U;
             (index < main_TASK_COUNT) && (result == main_TASK_RESULT_SUCCESS);
             ++index)
        {
            ret = pthread_create(&task_ids[index], NULL, task_entries[index], NULL);
            if (ret != 0)
            {
                result = main_TASK_RESULT_FAILURE;
            }
            else
            {
                ++created_count;
            }
        }

        /* 성공한 create가 기록한 ID만 읽는다. 실패해도 나머지 join은 수행한다. */
        for (index = 0U; index < created_count; ++index)
        {
            ret = pthread_join(task_ids[index], NULL);
            if (ret != 0)
            {
                result = main_TASK_RESULT_FAILURE;
            }
        }

        /* main()은 이 스레드의 join 성공 후에만 결과를 읽는다. */
        *task_result = result;
    }

    return NULL;
}
