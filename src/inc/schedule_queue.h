#pragma once
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Queue stored away from SCHED_BASE. Change if your map needs it. */
#define SCHEDULE_QUEUE_BASE ((uint16_t)0x0440u)

#define SCHEDULE_QUEUE_CAP 5u
#define SCHEDULE_QUEUE_TIME_LEN 7u /* same as SCHED_TIME_LEN */

/* Layout (linear queue, no ring):
   [BASE + 0] : count (0..CAP); 0xFF => uninitialized
   [BASE + 1] : entries area (CAP * ENTRY_SIZE), entry i at
                BASE + 1 + i*ENTRY_SIZE
*/
#define SCHEDULE_QUEUE_COUNT_OFF (SCHEDULE_QUEUE_BASE + 0u)
#define SCHEDULE_QUEUE_ENTRIES_OFF (SCHEDULE_QUEUE_BASE + 1u)

#define SCHEDULE_QUEUE_ENTRY_SIZE (SCHEDULE_QUEUE_TIME_LEN + 1u)
#define SCHEDULE_QUEUE_TOTAL_LEN (1u + (SCHEDULE_QUEUE_CAP * SCHEDULE_QUEUE_ENTRY_SIZE))

   void schedule_queue_init_if_blank(void);
   void schedule_queue_clear(void);
   uint8_t schedule_queue_count(void);
   void schedule_queue_log(void);
   int schedule_queue_push(const uint8_t time7[SCHEDULE_QUEUE_TIME_LEN], uint8_t intensity2b);
   int schedule_queue_peek(uint8_t out_time7[SCHEDULE_QUEUE_TIME_LEN], uint8_t *out_int2b);
   int schedule_queue_pop(uint8_t out_time7[SCHEDULE_QUEUE_TIME_LEN], uint8_t *out_int2b);
   int schedule_queue_rebuild_from_sched(void);
   int schedule_queue_sync_and_arm_next(void);
   int schedule_queue_on_alarm(void (*do_action)(uint8_t intensity, const struct tm *when));

#ifdef __cplusplus
}
#endif
