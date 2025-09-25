/*
 * ds3231.c - DS3231 RTC Driver Implementation
 */

#include "ds3231.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(ds3231, LOG_LEVEL_INF);

/* I2C device binding */
#define I2C_NODE DT_NODELABEL(i2c0)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

/* BCD conversion helpers */
static inline uint8_t bcd_to_bin(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static inline uint8_t bin_to_bcd(uint8_t bin)
{
    return ((bin / 10) << 4) | (bin % 10);
}

/* Low-level I2C functions */
static int ds3231_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_write(i2c_dev, buf, sizeof(buf), DS3231_ADDR);
}

static int ds3231_read_reg(uint8_t reg, uint8_t *data)
{
    return i2c_write_read(i2c_dev, DS3231_ADDR, &reg, 1, data, 1);
}

static int ds3231_read_burst(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_write_read(i2c_dev, DS3231_ADDR, &reg, 1, data, len);
}

/* Public API functions */
int ds3231_init(void)
{
    if (!device_is_ready(i2c_dev))
    {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    /* Try to read control register to verify device presence */
    uint8_t control;
    int ret = ds3231_read_reg(DS3231_REG_CONTROL, &control);
    if (ret)
    {
        LOG_ERR("DS3231 probe failed (%d)", ret);
        return ret;
    }

    LOG_INF("DS3231 found at 0x%02X (Control=0x%02X)", DS3231_ADDR, control);

    /* Configure DS3231: Enable oscillator, disable square wave */
    ret = ds3231_write_reg(DS3231_REG_CONTROL, 0x04);
    if (ret)
    {
        LOG_ERR("Failed to write control register (%d)", ret);
        return ret;
    }

    /* Clear status flags */
    ret = ds3231_write_reg(DS3231_REG_STATUS, 0x00);
    if (ret)
    {
        LOG_ERR("Failed to write status register (%d)", ret);
        return ret;
    }

    LOG_INF("DS3231 initialized successfully");
    return 0;
}

int ds3231_set_time(const struct ds3231_time *time)
{
    if (!time)
    {
        return -EINVAL;
    }

    uint8_t time_data[7];
    time_data[0] = bin_to_bcd(time->second);
    time_data[1] = bin_to_bcd(time->minute);
    time_data[2] = bin_to_bcd(time->hour);
    time_data[3] = bin_to_bcd(time->day_of_week);
    time_data[4] = bin_to_bcd(time->date);
    time_data[5] = bin_to_bcd(time->month);
    time_data[6] = bin_to_bcd(time->year);

    uint8_t buf[8];
    buf[0] = DS3231_REG_SECONDS;
    memcpy(&buf[1], time_data, 7);

    int ret = i2c_write(i2c_dev, buf, sizeof(buf), DS3231_ADDR);
    if (ret)
    {
        LOG_ERR("Failed to set time (%d)", ret);
        return ret;
    }

    LOG_INF("Time set: %02u/%02u/20%02u %02u:%02u:%02u (DoW: %u)",
            time->date, time->month, time->year,
            time->hour, time->minute, time->second, time->day_of_week);
    return 0;
}

int ds3231_get_time(struct ds3231_time *time)
{
    if (!time)
    {
        return -EINVAL;
    }

    uint8_t time_data[7];
    int ret = ds3231_read_burst(DS3231_REG_SECONDS, time_data, sizeof(time_data));
    if (ret)
    {
        LOG_ERR("Failed to read time (%d)", ret);
        return ret;
    }

    time->second = bcd_to_bin(time_data[0] & 0x7F);
    time->minute = bcd_to_bin(time_data[1] & 0x7F);
    time->hour = bcd_to_bin(time_data[2] & 0x3F);
    time->day_of_week = bcd_to_bin(time_data[3] & 0x07);
    time->date = bcd_to_bin(time_data[4] & 0x3F);
    time->month = bcd_to_bin(time_data[5] & 0x1F);
    time->year = bcd_to_bin(time_data[6]);

    return 0;
}

int ds3231_get_temperature(int16_t *temp_c, uint8_t *temp_frac)
{
    if (!temp_c || !temp_frac)
    {
        return -EINVAL;
    }

    uint8_t temp_data[2];
    int ret = ds3231_read_burst(DS3231_REG_TEMP_MSB, temp_data, 2);
    if (ret)
    {
        LOG_ERR("Failed to read temperature (%d)", ret);
        return ret;
    }

    /* Temperature: MSB is signed integer, LSB[7:6] is fractional */
    *temp_c = (int8_t)temp_data[0];        /* Sign extend */
    *temp_frac = (temp_data[1] >> 6) * 25; /* Convert to hundredths */

    return 0;
}

int ds3231_is_running(bool *running)
{
    if (!running)
    {
        return -EINVAL;
    }

    uint8_t status;
    int ret = ds3231_read_reg(DS3231_REG_STATUS, &status);
    if (ret)
    {
        return ret;
    }

    /* OSF bit (bit 7) indicates oscillator stopped */
    *running = !(status & 0x80);
    return 0;
}