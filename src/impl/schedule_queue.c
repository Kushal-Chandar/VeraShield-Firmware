#include <string.h>
#include "schedule_queue.h"
#include "schedule.h"
#include "at24c32.h"
#include "tm_helpers.h"
#include "pcf8563.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* ---- AT24 helpers ---- */
static int rd8(uint16_t addr, uint8_t *v) { return at24c32_read_byte(addr, v); }
static int wr8(uint16_t addr, uint8_t v) { return at24c32_write_byte(addr, v); }
static int rdb(uint16_t a, void *buf, size_t len) { return at24c32_read_bytes(a, (uint8_t *)buf, len); }
static int wrb(uint16_t a, const void *buf, size_t len) { return at24c32_write_bytes(a, (const uint8_t *)buf, len); }

/* Entry i address (linear) */
static inline uint16_t entry_addr(uint8_t i)
{
    return (uint16_t)(SCHEDULE_QUEUE_ENTRIES_OFF + ((uint16_t)i * SCHEDULE_QUEUE_ENTRY_SIZE));
}

/* Read/write one entry */
static int read_entry(uint8_t i, uint8_t time7[7], uint8_t *int2b)
{
    if (!time7)
        return -1;
    const uint16_t a = entry_addr(i);
    uint8_t inten = 0;
    if (rdb(a, time7, 7) != 0)
        return -1;
    if (rd8((uint16_t)(a + 7u), &inten) != 0)
        return -1;
    if (int2b)
        *int2b = (uint8_t)(inten & 0x03u);
    return 0;
}
static int write_entry(uint8_t i, const uint8_t time7[7], uint8_t int2b)
{
    if (!time7)
        return -1;
    const uint16_t a = entry_addr(i);
    const uint8_t inten = (uint8_t)(int2b & 0x03u);
    if (wrb(a, time7, 7) != 0)
        return -1;
    if (wr8((uint16_t)(a + 7u), inten) != 0)
        return -1;
    return 0;
}

/* Compare two struct tm chronologically */

/* ---- Public API ---- */

void schedule_queue_init_if_blank(void)
{
    uint8_t cnt;
    if (rd8(SCHEDULE_QUEUE_COUNT_OFF, &cnt) != 0)
    {
        /* If read fails, force-init */
        cnt = 0xFFu;
    }
    if (cnt == 0xFFu)
    {
        (void)wr8(SCHEDULE_QUEUE_COUNT_OFF, 0u);

        /* Optional: clear entries region (<= 40 bytes total) */
        uint8_t zeros[AT24C32_PAGE_SIZE] = {0};
        const size_t total = (size_t)SCHEDULE_QUEUE_CAP * SCHEDULE_QUEUE_ENTRY_SIZE;
        size_t off = 0;
        while (off < total)
        {
            const size_t chunk = MIN(sizeof(zeros), total - off);
            (void)wrb((uint16_t)(SCHEDULE_QUEUE_ENTRIES_OFF + off), zeros, chunk);
            off += chunk;
        }
    }
}

void schedule_queue_clear(void)
{
    (void)wr8(SCHEDULE_QUEUE_COUNT_OFF, 0u);
}

uint8_t schedule_queue_count(void)
{
    uint8_t cnt = 0;
    if (rd8(SCHEDULE_QUEUE_COUNT_OFF, &cnt) != 0)
        return 0;
    if (cnt > SCHEDULE_QUEUE_CAP)
    { /* sanitize if corrupted */
        (void)wr8(SCHEDULE_QUEUE_COUNT_OFF, 0u);
        return 0;
    }
    return cnt;
}

/* Append at end (no wrap). */
int schedule_queue_push(const uint8_t time7[7], uint8_t intensity2b)
{
    if (!time7)
        return -1;

    uint8_t cnt;
    if (rd8(SCHEDULE_QUEUE_COUNT_OFF, &cnt) != 0)
        return -1;
    if (cnt > SCHEDULE_QUEUE_CAP)
    { /* sanitize */
        (void)wr8(SCHEDULE_QUEUE_COUNT_OFF, 0u);
        return -1;
    }
    if (cnt >= SCHEDULE_QUEUE_CAP)
        return -1; /* full */

    if (write_entry(cnt, time7, (uint8_t)(intensity2b & 0x03u)) != 0)
        return -1;
    if (wr8(SCHEDULE_QUEUE_COUNT_OFF, (uint8_t)(cnt + 1u)) != 0)
        return -1;
    return 0;
}

/* Read first entry only */
int schedule_queue_peek(uint8_t out_time7[7], uint8_t *out_int2b)
{
    if (!out_time7)
        return -1;

    uint8_t cnt;
    if (rd8(SCHEDULE_QUEUE_COUNT_OFF, &cnt) != 0)
        return -1;
    if (cnt == 0u || cnt > SCHEDULE_QUEUE_CAP)
        return -1;

    return read_entry(0u, out_time7, out_int2b);
}

/* Pop first entry and shift remaining left by one (max 4 moves => fine). */
int schedule_queue_pop(uint8_t out_time7[7], uint8_t *out_int2b)
{
    uint8_t cnt;
    if (rd8(SCHEDULE_QUEUE_COUNT_OFF, &cnt) != 0)
        return -1;
    if (cnt == 0u || cnt > SCHEDULE_QUEUE_CAP)
        return -1;

    /* optionally return the popped data */
    if (out_time7 || out_int2b)
    {
        if (read_entry(0u, out_time7 ? out_time7 : (uint8_t[7]){0}, out_int2b) != 0)
            return -1;
    }

    /* Compact if there are remaining entries */
    if (cnt > 1u)
    {
        /* Move bytes [entry 1 .. entry cnt-1] -> [entry 0 .. entry cnt-2] */
        const size_t move_bytes = (size_t)(cnt - 1u) * SCHEDULE_QUEUE_ENTRY_SIZE;
        uint8_t buf[(SCHEDULE_QUEUE_CAP - 1u) * SCHEDULE_QUEUE_ENTRY_SIZE];

        if (rdb(entry_addr(1u), buf, move_bytes) != 0)
            return -1;
        if (wrb(entry_addr(0u), buf, move_bytes) != 0)
            return -1;
    }

    /* Optionally clear last slot (not required for correctness) */
    /*
    {
        uint8_t zeros[SCHEDULE_QUEUE_ENTRY_SIZE] = {0};
        (void)wrb(entry_addr((uint8_t)(cnt - 1u)), zeros, sizeof(zeros));
    }
    */

    /* Decrement count */
    if (wr8(SCHEDULE_QUEUE_COUNT_OFF, (uint8_t)(cnt - 1u)) != 0)
        return -1;
    return 0;
}

/* Build a sorted (by time) linear list starting at index 0 */
int schedule_queue_rebuild_from_sched(void)
{
    struct item
    {
        struct tm t;
        uint8_t time7[7];
        uint8_t inten2b;
    } items[SCHEDULE_QUEUE_CAP];

    uint8_t n = 0;
    const uint8_t n_sched = sched_count();

    for (uint8_t i = 0; i < n_sched && n < SCHEDULE_QUEUE_CAP; ++i)
    {
        struct tm tmv;
        uint8_t inten = 0;
        uint8_t t7[7];

        if (sched_get(i, t7, &inten) != 0)
            continue;
        if (sched_get_tm(i, &tmv, &inten) != 0)
            continue;

        memcpy(items[n].time7, t7, 7);
        items[n].inten2b = (uint8_t)(inten & 0x03u);
        items[n].t = tmv;
        ++n;
    }

    /* insertion sort by time asc */
    for (uint8_t i = 1; i < n; ++i)
    {
        struct item key = items[i];
        int j = (int)i - 1;
        while (j >= 0 && tm_cmp(&items[j].t, &key.t) > 0)
        {
            items[j + 1] = items[j];
            --j;
        }
        items[j + 1] = key;
    }

    /* Write contiguous entries starting at 0, set count=n */
    if (wr8(SCHEDULE_QUEUE_COUNT_OFF, 0u) != 0)
        return -1; /* reset first */

    for (uint8_t i = 0; i < n; ++i)
    {
        if (write_entry(i, items[i].time7, items[i].inten2b) != 0)
            return -1;
    }
    if (wr8(SCHEDULE_QUEUE_COUNT_OFF, n) != 0)
        return -1;

    /* Optional: clear any leftover slots */
    /*
    if (n < SCHEDULE_QUEUE_CAP) {
        uint8_t zeros[SCHEDULE_QUEUE_ENTRY_SIZE] = {0};
        for (uint8_t i = n; i < SCHEDULE_QUEUE_CAP; ++i) {
            (void)wrb(entry_addr(i), zeros, sizeof(zeros));
        }
    }
    */

    return n;
}

static inline struct pcf8563 *rtc(void) { return pcf8563_get(); }

static int rtc_now(struct tm *out)
{
    struct pcf8563 *r = rtc();
    return (r && out) ? pcf8563_get_time(r, out) : -1;
}

/* PCF8563 alarm is minute-resolution; arm at H:M for the next entry. */
static int rtc_alarm_arm_hm(const struct tm *t)
{
    struct pcf8563 *r = rtc();
    if (!r || !t)
        return -1;

    int rc = pcf8563_set_alarm_hm(r, t->tm_hour, t->tm_min);
    if (rc)
        return rc;
    rc = pcf8563_alarm_clear_flag(r);
    if (rc)
        return rc;
    return pcf8563_alarm_irq_enable(r, true);
}

/*
 * Clean past entries and arm the next future one (no pop).
 * Behavior:
 *   - If queue is empty, rebuild from sched_* once.
 *   - Drop all entries with time <= now.
 *   - Stop at the first entry > now, DO NOT pop; arm RTC at its H:M.
 *
 * Returns:
 *    0 = armed next future entry
 *    1 = nothing to arm (empty after rebuild/cleanup)
 *   -1 = RTC read error
 *   -2 = EEPROM/queue I/O error
 *   -3 = RTC arm error
 */
int schedule_queue_sync_and_arm_next(void)
{
    struct tm now = {0};
    if (rtc_now(&now) != 0)
        return -1;

    /* If empty, rebuild once */
    if (schedule_queue_count() == 0)
    {
        int n = schedule_queue_rebuild_from_sched();
        if (n < 0)
            return -2;
        if (n == 0)
            return 1; /* still nothing */
    }

    /* Drop all stale entries (<= now) */
    for (;;)
    {
        if (schedule_queue_count() == 0)
            return 1;

        uint8_t t7[7], inten;
        if (schedule_queue_peek(t7, &inten) != 0)
            return -2;

        struct tm head = {0};
        tm_from_7(&head, t7);

        if (!tm_sane(&head))
        {
            (void)schedule_queue_pop(NULL, NULL);
            continue;
        }

        if (tm_cmp(&head, &now) <= 0)
        {
            (void)schedule_queue_pop(NULL, NULL);
            continue;
        }

        /* head > now: leave it in place, just arm */
        return (rtc_alarm_arm_hm(&head) == 0) ? 0 : -3;
    }
}

/*
 * Alarm handler helper.
 * Call this from your PCF8563 alarm callback/work item.
 * Flow:
 *   - clear/disable RTC alarm
 *   - peek head (still present), run action with intensity
 *   - pop exactly one
 *   - arm next future entry
 */
int schedule_queue_on_alarm(void (*do_action)(uint8_t intensity, const struct tm *when))
{
    struct pcf8563 *r = rtc();
    if (r)
    {
        (void)pcf8563_alarm_clear_flag(r);
        (void)pcf8563_alarm_irq_enable(r, false); /* avoid retrigger storms */
    }

    uint8_t t7[7], inten = 0;
    if (schedule_queue_peek(t7, &inten) == 0)
    {
        struct tm when = {0};
        tm_from_7(&when, t7);
        if (tm_sane(&when) && do_action)
        {
            do_action((uint8_t)(inten & 0x03u), &when);
        }
        (void)schedule_queue_pop(NULL, NULL);
    }

    return schedule_queue_sync_and_arm_next();
}