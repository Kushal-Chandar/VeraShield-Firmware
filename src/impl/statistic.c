#include "statistic.h"
#include "at24c32.h"
#include "pcf8563.h"
#include <string.h>
#include <errno.h>

int statistic_load(uint16_t *count, uint8_t *state, struct tm *t_out)
{
    uint8_t buf[STAT_LEN_BYTES] = {0};
    int rc = at24c32_read_bytes(STAT_ADDR_BASE, buf, sizeof buf);
    if (rc)
        return rc;

    uint16_t v = ((uint16_t)buf[0] << 8) | buf[1];
    if (count)
        *count = stat_unpack_count(v);
    if (state)
        *state = stat_unpack_state(v);
    if (t_out)
        tm_from_7(t_out, &buf[STAT_TIME_OFS]);
    return 0;
}

int statistic_save(uint16_t count, uint8_t state, const struct tm *t)
{
    if (t == NULL)
        return -EINVAL;

    if (!tm_sane(t))
        return -ERANGE;

    uint8_t buf[STAT_LEN_BYTES] = {0};
    uint16_t v = stat_pack(count, state);
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)(v & 0xFF);

    tm_to_7(t, &buf[STAT_TIME_OFS]);

    return at24c32_write_page(STAT_ADDR_BASE, buf, sizeof buf);
}

int statistic_increment_with_rtc(struct pcf8563 *rtc, uint8_t state)
{
    if (rtc == NULL)
        return -ENODEV;

    uint16_t count = 0;
    uint8_t prev_state = 0;
    (void)statistic_load(&count, &prev_state, NULL);

    count = (uint16_t)((count + 1) & STAT_COUNT_MASK);

    struct tm now_tm;
    int rc = pcf8563_get_time(rtc, &now_tm);
    if (rc)
        return rc;

    return statistic_save(count, state, &now_tm);
}
