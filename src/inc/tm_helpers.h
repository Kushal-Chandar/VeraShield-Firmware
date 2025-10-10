#pragma once
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

bool tm_sane(const struct tm *t);
void tm_to_7(const struct tm *t, uint8_t out[7]);
void tm_from_7(struct tm *t, const uint8_t in[7]);
int tm_cmp(const struct tm *a, const struct tm *b);

static inline const char *tm_to_str(const struct tm *t, char *buf, size_t len)
{
    /* tm_year is years since 1900, tm_mon is 0..11 */
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d (wday=%d)",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec, t->tm_wday);
    return buf;
}
