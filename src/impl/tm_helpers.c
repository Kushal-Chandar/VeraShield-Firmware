#include "tm_helpers.h"
#include <string.h>

bool tm_sane(const struct tm *t)
{
    int y = t->tm_year + 1900;
    return (y >= 2000 && y <= 2099) &&
           (t->tm_mon >= 0 && t->tm_mon < 12) &&
           (t->tm_mday >= 1 && t->tm_mday <= 31) &&
           (t->tm_hour >= 0 && t->tm_hour < 24) &&
           (t->tm_min >= 0 && t->tm_min < 60) &&
           (t->tm_sec >= 0 && t->tm_sec < 60);
}

// These functions are pure conversion no processing
void tm_to_7(const struct tm *t, uint8_t out[7])
{
    out[0] = (uint8_t)t->tm_sec;
    out[1] = (uint8_t)t->tm_min;
    out[2] = (uint8_t)t->tm_hour;
    out[3] = (uint8_t)t->tm_mday;
    out[4] = (uint8_t)t->tm_wday;
    out[5] = (uint8_t)t->tm_mon;    /* 0..11 */
    out[6] = (uint8_t)(t->tm_year); /* years since 1900 */
}

void tm_from_7(struct tm *t, const uint8_t in[7])
{
    memset(t, 0, sizeof(*t));
    t->tm_sec = in[0];
    t->tm_min = in[1];
    t->tm_hour = in[2];
    t->tm_mday = in[3];
    t->tm_wday = in[4];
    t->tm_mon = in[5];  /* 0..11 */
    t->tm_year = in[6]; /* years since 1900 */
}