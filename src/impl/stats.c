#include "stats.h"
#include "at24c32.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include "pcf8563.h"
#include "tm_helpers.h"

LOG_MODULE_REGISTER(stats, LOG_LEVEL_INF);

/* ========= local helpers ========= */

static inline uint32_t time_addr(uint8_t idx)
{
    return (uint32_t)STATS_TIMES_OFF + (uint32_t)idx * TIME_LEN;
}
static inline uint16_t inten_byte_addr(uint8_t idx)
{
    return (uint16_t)(STATS_INT_OFF + (idx >> 2)); // idx/4
}
static inline uint8_t inten_shift(uint8_t idx)
{
    return (uint8_t)((idx & 0x3u) * 2u);
}

/* ========= public API ========= */

void stats_init_if_blank(void)
{
    uint8_t cnt = 0xFF;
    int rc = at24c32_read_byte(STATS_COUNT_OFF, &cnt);
    if (rc)
    {
        LOG_ERR("stats_init_if_blank: read count failed (%d)", rc);
        return;
    }

    // Without a magic byte: treat any invalid (> STATS_CAP) value (incl. 0xFF) as blank and zero it.
    if (cnt > STATS_CAP)
    {
        (void)at24c32_write_byte(STATS_COUNT_OFF, 0);
        LOG_INF("stats: initialized count=0 @0x%04X (was 0x%02X)", STATS_COUNT_OFF, cnt);
    }
}

uint8_t stats_count(void)
{
    uint8_t cnt = 0;
    int rc = at24c32_read_byte(STATS_COUNT_OFF, &cnt);
    if (rc)
    {
        LOG_ERR("stats_count: read failed (%d)", rc);
        return 0;
    }
    if (cnt > STATS_CAP)
        return 0; // invalid persisted value; treat as empty
    return cnt;
}

int stats_append(const uint8_t time[TIME_LEN], uint8_t intensity2b)
{
    if (!time)
        return 0;

    uint8_t cnt = stats_count();
    if (cnt >= STATS_CAP)
    {
        LOG_WRN("stats_append: capacity reached (%u), not appending", (unsigned)STATS_CAP);
        return 0; // stop when full; ask if you want ring overwrite
    }

    // 1) Write time first (driver will split across page boundaries)
    const uint32_t taddr = time_addr(cnt);
    int rc = at24c32_write_bytes((uint16_t)taddr, time, TIME_LEN);
    if (rc)
    {
        LOG_ERR("stats_append: write time failed (%d)", rc);
        return 0;
    }

    // 2) Pack the 2-bit intensity into its byte
    const uint16_t ibyte = inten_byte_addr(cnt);
    const uint8_t sh = inten_shift(cnt);
    const uint8_t mask = (uint8_t)(0x03u << sh);
    const uint8_t val = (uint8_t)((intensity2b & 0x03u) << sh);

    rc = at24c32_update_bits(ibyte, mask, val);
    if (rc)
    {
        LOG_ERR("stats_append: update intensity failed (%d)", rc);
        return 0;
    }

    // 3) Bump count last (power-loss friendly)
    rc = at24c32_write_byte(STATS_COUNT_OFF, (uint8_t)(cnt + 1));
    if (rc)
    {
        LOG_ERR("stats_append: bump count failed (%d)", rc);
        return 0;
    }

    return 1;
}

int stats_get(uint8_t index, uint8_t out_time[TIME_LEN], uint8_t *out_int2b)
{
    uint8_t cnt = stats_count();
    if (index >= cnt)
        return 0;

    // read time
    int rc = at24c32_read_bytes((uint16_t)time_addr(index), out_time, TIME_LEN);
    if (rc)
    {
        LOG_ERR("stats_get: read time failed (%d)", rc);
        return 0;
    }

    // read intensity byte then extract 2 bits
    uint8_t ib = 0;
    rc = at24c32_read_byte(inten_byte_addr(index), &ib);
    if (rc)
    {
        LOG_ERR("stats_get: read intensity byte failed (%d)", rc);
        return 0;
    }

    if (out_int2b)
    {
        *out_int2b = (uint8_t)((ib >> inten_shift(index)) & 0x03u);
    }
    return 1;
}

void stats_clear(void)
{
    (void)at24c32_write_byte(STATS_COUNT_OFF, 0);
    // Optional: zero intensity region for cleanliness (not required).
    // for (uint32_t a = STATS_INT_OFF; a < STATS_INT_OFF + STATS_INT_LEN; ++a)
    //     (void)at24c32_write_byte((uint16_t)a, 0);
}

/* ---------- struct tm wrappers ---------- */

int stats_append_tm(const struct tm *t, uint8_t intensity2b)
{
    if (!t)
        return 0;
    uint8_t buf[TIME_LEN];
    tm_to_7(t, buf);
    return stats_append(buf, intensity2b);
}

int stats_get_tm(uint8_t index, struct tm *out_t, uint8_t *out_int2b)
{
    if (!out_t)
        return 0;
    uint8_t buf[TIME_LEN];
    int ok = stats_get(index, buf, out_int2b);
    if (!ok)
        return 0;
    tm_from_7(out_t, buf);
    return 1;
}
