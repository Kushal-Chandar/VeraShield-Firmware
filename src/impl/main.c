#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include "pcf8563.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define PCF8563_NODE DT_NODELABEL(pcf8563)

static struct pcf8563 rtc = {
    .i2c = I2C_DT_SPEC_GET(PCF8563_NODE),
    .int_gpio = GPIO_DT_SPEC_GET(PCF8563_NODE, int_gpios),
};

static void arm_next_minute(void);

static void on_alarm(void *user)
{
    ARG_UNUSED(user);
    LOG_INF("RTC alarm fired");
    arm_next_minute();
}

static bool tm_sane(const struct tm *t)
{
    int y = t->tm_year + 1900;
    return (y >= 2000 && y <= 2099) &&
           (t->tm_mon >= 0 && t->tm_mon < 12) &&
           (t->tm_mday >= 1 && t->tm_mday <= 31) &&
           (t->tm_hour >= 0 && t->tm_hour < 24) &&
           (t->tm_min >= 0 && t->tm_min < 60) &&
           (t->tm_sec >= 0 && t->tm_sec < 60);
}

static void seed_time_from_build_if_needed(void)
{
    struct tm now;
    if (pcf8563_get_time(&rtc, &now) == 0 && tm_sane(&now))
        return;

    static const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {0};
    int d, y, H, M, S;
    if (sscanf(__DATE__, "%3s %d %d", m, &d, &y) != 3)
        return;
    if (sscanf(__TIME__, "%d:%d:%d", &H, &M, &S) != 3)
        return;
    const char *p = strstr(mons, m);
    int mon = p ? (int)((p - mons) / 3) : 0;

    struct tm t = {
        .tm_sec = S, .tm_min = M, .tm_hour = H, .tm_mday = d, .tm_mon = mon, .tm_year = y - 1900, .tm_isdst = -1};
    if (pcf8563_set_time(&rtc, &t) == 0)
    {
        LOG_INF("RTC seeded: %04d-%02d-%02d %02d:%02d:%02d",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
    }
}

static void arm_next_minute(void)
{
    struct tm now;
    if (pcf8563_get_time(&rtc, &now) != 0)
    {
        LOG_ERR("get_time failed");
        return;
    }

    int next_min = (now.tm_min + 1) % 60;
    int hour = now.tm_hour;
    if (next_min == 0)
    {
        hour = (hour + 1) % 24; /* handle hour rollover */
    }

    int rc = pcf8563_set_alarm_hm(&rtc, hour, next_min);
    if (rc)
    {
        LOG_ERR("set_alarm_hm failed: %d", rc);
        return;
    }

    LOG_INF("Now %04d-%02d-%02d %02d:%02d:%02d; armed alarm at %02d:%02d",
            now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
            now.tm_hour, now.tm_min, now.tm_sec,
            hour, next_min);
}

void main(void)
{
    int rc = pcf8563_init(&rtc);
    if (rc)
    {
        LOG_ERR("RTC init failed: %d", rc);
        return;
    }

    pcf8563_set_alarm_callback(&rtc, on_alarm, NULL);

    /* Make sure INT is released before arming */
    (void)pcf8563_alarm_clear_flag(&rtc);

    seed_time_from_build_if_needed();
    arm_next_minute();

    for (;;)
    {
        /* Optional heartbeat log every 10s */
        struct tm now;
        if (pcf8563_get_time(&rtc, &now) == 0)
        {
            LOG_INF("Now %04d-%02d-%02d %02d:%02d:%02d",
                    now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                    now.tm_hour, now.tm_min, now.tm_sec);
        }
        k_sleep(K_SECONDS(10));
    }
}
