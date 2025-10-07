#include <string.h>
#include <stdint.h>
#include <time.h>

#include "schedule.h"
#include "tm_helpers.h"
#include "at24c32.h"

static int rd_count(uint8_t *c)
{
    return at24c32_read_bytes(SCHED_COUNT_OFF, c, 1) ? -1 : 0;
}
static int wr_count(uint8_t c)
{
    return at24c32_write_bytes(SCHED_COUNT_OFF, &c, 1) ? -1 : 0;
}

static int rd_time_i(uint8_t i, uint8_t *t7)
{
    const uint16_t off = (uint16_t)(SCHED_TIMES_OFF + (uint16_t)i * SCHED_TIME_LEN);
    return at24c32_read_bytes(off, t7, SCHED_TIME_LEN) ? -1 : 0;
}
static int wr_time_i(uint8_t i, const uint8_t *t7)
{
    const uint16_t off = (uint16_t)(SCHED_TIMES_OFF + (uint16_t)i * SCHED_TIME_LEN);
    return at24c32_write_bytes(off, t7, SCHED_TIME_LEN) ? -1 : 0;
}

static int rd_int_byte(uint8_t idx, uint8_t *b)
{
    return at24c32_read_bytes((uint16_t)(SCHED_INT_OFF + idx), b, 1) ? -1 : 0;
}
static int wr_int_byte(uint8_t idx, uint8_t b)
{
    return at24c32_write_bytes((uint16_t)(SCHED_INT_OFF + idx), &b, 1) ? -1 : 0;
}

static int get_intensity(uint8_t i, uint8_t *out2b)
{
    uint8_t b, shift = (uint8_t)((i & 3u) * 2u);
    if (rd_int_byte(i >> 2, &b))
        return -1;
    *out2b = (uint8_t)((b >> shift) & 0x3u);
    return 0;
}
static int set_intensity(uint8_t i, uint8_t v)
{
    if (v > 3u)
        return -2;
    uint8_t b, idx = i >> 2, shift = (uint8_t)((i & 3u) * 2u);
    if (rd_int_byte(idx, &b))
        return -1;
    b = (uint8_t)((b & (uint8_t)~(0x3u << shift)) | ((v & 0x3u) << shift));
    return wr_int_byte(idx, b) ? -1 : 0;
}

void sched_init_if_blank(void)
{
    uint8_t c;
    if (rd_count(&c))
        return;
    if (c == 0xFFu)
    {
        (void)wr_count(0u);
        for (uint8_t i = 0; i < SCHED_INT_LEN; ++i)
            (void)wr_int_byte(i, 0u);
        uint8_t z[SCHED_TIME_LEN] = {0};
        for (uint8_t i = 0; i < SCHED_CAP; ++i)
            (void)wr_time_i(i, z);
    }
}

int sched_append(const uint8_t time7[SCHED_TIME_LEN], uint8_t intensity2b)
{
    uint8_t c;
    if (rd_count(&c))
        return -3;
    if (c >= SCHED_CAP)
        return -1;
    if (intensity2b > 3u)
        return -2;
    if (wr_time_i(c, time7))
        return -3;
    if (set_intensity(c, intensity2b))
        return -3;
    if (wr_count((uint8_t)(c + 1u)))
        return -3;
    return (int)c;
}

int sched_get(uint8_t index, uint8_t out_time7[SCHED_TIME_LEN], uint8_t *out_int2b)
{
    uint8_t c;
    if (rd_count(&c))
        return -3;
    if (index >= c)
        return -1;
    if (rd_time_i(index, out_time7))
        return -3;
    if (out_int2b && get_intensity(index, out_int2b))
        return -3;
    return 0;
}

uint8_t sched_count(void)
{
    uint8_t c = 0;
    if (rd_count(&c))
        return 0;
    if (c == 0xFFu)
        return 0;
    if (c > SCHED_CAP)
        c = SCHED_CAP;
    return c;
}

void sched_clear(void)
{
    (void)wr_count(0u);
}

int sched_append_tm(const struct tm *t, uint8_t intensity2b)
{
    if (!tm_sane(t) || intensity2b > 3u)
        return -2;
    uint8_t b[SCHED_TIME_LEN];
    tm_to_7(t, b);
    return sched_append(b, intensity2b);
}

int sched_get_tm(uint8_t index, struct tm *out_t, uint8_t *out_int2b)
{
    uint8_t b[SCHED_TIME_LEN];
    int r = sched_get(index, b, out_int2b);
    if (r < 0)
        return r;
    if (out_t)
        tm_from_7(out_t, b);
    return 0;
}
