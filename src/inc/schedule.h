#pragma once
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Layout (unchanged) */
#define SCHED_BASE 0x0400u
#define SCHED_CAP 5u
#define SCHED_TIME_LEN 7u

#define SCHED_COUNT_OFF (SCHED_BASE + 0u)
#define SCHED_TIMES_OFF (SCHED_BASE + 1u)
#define SCHED_TIMES_LEN ((uint32_t)SCHED_CAP * SCHED_TIME_LEN)
#define SCHED_INT_OFF (SCHED_TIMES_OFF + SCHED_TIMES_LEN)
#define SCHED_INT_LEN ((SCHED_CAP + 3u) / 4u)
#define SCHED_TOTAL_LEN (1u + SCHED_TIMES_LEN + SCHED_INT_LEN)

    /* Public API (unchanged) */
    void sched_init_if_blank(void);
    int sched_append(const uint8_t time7[SCHED_TIME_LEN], uint8_t intensity2b);
    int sched_get(uint8_t index, uint8_t out_time7[SCHED_TIME_LEN], uint8_t *out_int2b);
    uint8_t sched_count(void);
    void sched_clear(void);
    int sched_append_tm(const struct tm *t, uint8_t intensity2b);
    int sched_get_tm(uint8_t index, struct tm *out_t, uint8_t *out_int2b);

#ifdef __cplusplus
}
#endif
