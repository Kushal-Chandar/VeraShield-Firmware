/*
 * DS3231.h - DS3231 RTC Driver Header
 */

#ifndef DS3231_H
#define DS3231_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* DS3231 I2C Address */
#define DS3231_ADDR 0x68

/* DS3231 Register Map */
#define DS3231_REG_SECONDS 0x00
#define DS3231_REG_MINUTES 0x01
#define DS3231_REG_HOURS 0x02
#define DS3231_REG_DAY 0x03
#define DS3231_REG_DATE 0x04
#define DS3231_REG_MONTH 0x05
#define DS3231_REG_YEAR 0x06
#define DS3231_REG_CONTROL 0x0E
#define DS3231_REG_STATUS 0x0F
#define DS3231_REG_TEMP_MSB 0x11
#define DS3231_REG_TEMP_LSB 0x12

/* Time structure */
struct ds3231_time
{
    uint8_t second;      /* 0-59 */
    uint8_t minute;      /* 0-59 */
    uint8_t hour;        /* 0-23 (24-hour format) */
    uint8_t day_of_week; /* 1-7 (1=Sunday) */
    uint8_t date;        /* 1-31 */
    uint8_t month;       /* 1-12 */
    uint8_t year;        /* 0-99 (20XX) */
};

/* Function prototypes */
int ds3231_init(void);
int ds3231_set_time(const struct ds3231_time *time);
int ds3231_get_time(struct ds3231_time *time);
int ds3231_get_temperature(int16_t *temp_c, uint8_t *temp_frac);
int ds3231_is_running(bool *running);

#endif /* DS3231_H */