#include "pcf8563.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pcf8563, LOG_LEVEL_INF);

#define REG_RTCSEC 0x00
#define REG_RTCMIN 0x01
#define REG_RTCHOUR 0x02
#define REG_RTCWKDAY 0x03
#define REG_RTCDATE 0x04
#define REG_RTCMTH 0x05
#define REG_RTCYEAR 0x06
#define REG_CONTROL 0x07

/* Alarm 0 registers */
#define REG_ALM0SEC 0x0A
#define REG_ALM0MIN 0x0B
#define REG_ALM0HOUR 0x0C
#define REG_ALM0WKDAY 0x0D
#define REG_ALM0DATE 0x0E
#define REG_ALM0MTH 0x0F

/* RTCSEC bits */
#define RTCSEC_ST BIT(7) /* start oscillator when 1 */

/* RTCWKDAY bits */
#define RTCWKDAY_VBATEN BIT(3) /* enable VBAT backup */

/* CONTROL bits (0x07) :contentReference[oaicite:2]{index=2} */
#define CONTROL_SQWEN BIT(6)
#define CONTROL_ALM1EN BIT(5)
#define CONTROL_ALM0EN BIT(4)
#define CONTROL_EXTOSC BIT(3)
#define CONTROL_CRSTRIM BIT(2)

/* ALM0WKDAY bits (0x0D) :contentReference[oaicite:3]{index=3} */
#define ALM0_ALMPOL BIT(7)
#define ALM0_MSK2 BIT(6)
#define ALM0_MSK1 BIT(5)
#define ALM0_MSK0 BIT(4)
#define ALM0_IF BIT(3)

/* I2C helpers */
static int rd(struct pcf8563 *d, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_burst_read_dt(&d->i2c, reg, buf, len);
}

static int wr(struct pcf8563 *d, uint8_t reg, const uint8_t *buf, size_t len)
{
    return i2c_burst_write_dt(&d->i2c, reg, buf, len);
}

static int wr8(struct pcf8563 *d, uint8_t reg, uint8_t val)
{
    return i2c_reg_write_byte_dt(&d->i2c, reg, val);
}

/* ---- Work item bound to each device ---- */

static void pcf8563_work_handler(struct k_work *work)
{
    struct pcf8563 *dev = CONTAINER_OF(work, struct pcf8563, work);

    if (dev->alarm_cb)
    {
        dev->alarm_cb(dev->alarm_user);
    }
}

/* ISR: DO NOT touch I2C here. Just schedule work. */
static void pcf8563_isr(const struct device *port,
                        struct gpio_callback *cb,
                        uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    struct pcf8563 *dev = CONTAINER_OF(cb, struct pcf8563, gpio_cb);
    (void)k_work_submit(&dev->work);
}

/* Public API */
static struct pcf8563 *g_dev;
void pcf8563_bind(struct pcf8563 *dev) { g_dev = dev; }
struct pcf8563 *pcf8563_get(void) { return g_dev; }

/* --- Alarm helpers mapped onto MCP7940N Alarm 0 --- */

int pcf8563_alarm_clear_flag(struct pcf8563 *dev)
{
    uint8_t w;
    int rc = rd(dev, REG_ALM0WKDAY, &w, 1);
    if (rc)
    {
        return rc;
    }

    /* Clearing ALM0IF: any write clears it; ensure bit3 = 0. :contentReference[oaicite:4]{index=4} */
    w &= ~ALM0_IF;
    return wr8(dev, REG_ALM0WKDAY, w);
}

int pcf8563_alarm_irq_enable(struct pcf8563 *dev, bool enable)
{
    uint8_t c;
    int rc = rd(dev, REG_CONTROL, &c, 1);
    if (rc)
    {
        return rc;
    }

    /* Make sure square-wave is off; we only use alarm output on MFP. :contentReference[oaicite:5]{index=5} */
    c &= ~(CONTROL_SQWEN | CONTROL_ALM1EN);

    if (enable)
    {
        c |= CONTROL_ALM0EN;
    }
    else
    {
        c &= ~CONTROL_ALM0EN;
    }

    return wr8(dev, REG_CONTROL, c);
}

/* --- Init --- */

int pcf8563_init(struct pcf8563 *dev)
{
    if (!device_is_ready(dev->i2c.bus))
    {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /* Make sure oscillator is running: set ST bit in RTCSEC if needed.  */
    uint8_t sec;
    if (rd(dev, REG_RTCSEC, &sec, 1) == 0)
    {
        if (!(sec & RTCSEC_ST))
        {
            sec |= RTCSEC_ST;
            (void)wr8(dev, REG_RTCSEC, sec);
        }
    }

    /* Enable VBAT backup (if wired) by setting VBATEN in RTCWKDAY.  */
    uint8_t wkday;
    if (rd(dev, REG_RTCWKDAY, &wkday, 1) == 0)
    {
        wkday |= RTCWKDAY_VBATEN;
        (void)wr8(dev, REG_RTCWKDAY, wkday);
    }

    /* Disable square wave and both alarms initially. */
    uint8_t ctrl = 0;
    if (rd(dev, REG_CONTROL, &ctrl, 1) == 0)
    {
        ctrl &= ~(CONTROL_SQWEN | CONTROL_ALM0EN | CONTROL_ALM1EN |
                  CONTROL_EXTOSC | CONTROL_CRSTRIM);
        (void)wr8(dev, REG_CONTROL, ctrl);
    }

    /* Clear any stale ALM0IF flag. */
    (void)pcf8563_alarm_clear_flag(dev);

    if (!device_is_ready(dev->int_gpio.port))
    {
        LOG_ERR("INT GPIO port not ready");
        return -ENODEV;
    }

    int rc = gpio_pin_configure_dt(&dev->int_gpio, GPIO_INPUT);
    if (rc)
    {
        return rc;
    }

    rc = gpio_pin_interrupt_configure_dt(&dev->int_gpio,
                                         GPIO_INT_EDGE_TO_ACTIVE);
    if (rc)
    {
        return rc;
    }

    gpio_init_callback(&dev->gpio_cb, pcf8563_isr, BIT(dev->int_gpio.pin));
    gpio_add_callback(dev->int_gpio.port, &dev->gpio_cb);

    k_work_init(&dev->work, pcf8563_work_handler);

    LOG_INF("MCP7940N (pcf8563 API) init ok (INT on %s.%u)",
            dev->int_gpio.port->name, dev->int_gpio.pin);
    return 0;
}

void pcf8563_set_alarm_callback(struct pcf8563 *dev,
                                void (*cb)(void *user), void *user)
{
    dev->alarm_cb = cb;
    dev->alarm_user = user;
}

/* --- Timekeeping --- */
/* Note: still assume 2000..2099, same as your old driver. */

int pcf8563_get_time(struct pcf8563 *dev, struct tm *t)
{
    uint8_t b[7];
    int rc = rd(dev, REG_RTCSEC, b, sizeof(b));
    if (rc)
    {
        return rc;
    }

    /* Seconds: mask off ST bit. */
    t->tm_sec = bcd2bin(b[0] & 0x7F);
    t->tm_min = bcd2bin(b[1] & 0x7F);
    t->tm_hour = bcd2bin(b[2] & 0x3F);

    /* Weekday: MCP7940N stores 1..7, user tm_wday is 0..6. :contentReference[oaicite:8]{index=8} */
    uint8_t wd_raw = b[3] & 0x07;
    uint8_t wd_val = bcd2bin(wd_raw); /* effectively same for 1..7 */
    if (wd_val == 0)
    {
        t->tm_wday = 0;
    }
    else
    {
        t->tm_wday = (wd_val - 1) % 7;
    }

    t->tm_mday = bcd2bin(b[4] & 0x3F);

    uint8_t month_bcd = b[5] & 0x1F;    /* mask off LPYR */
    t->tm_mon = bcd2bin(month_bcd) - 1; /* 0..11 */

    t->tm_year = 100 + bcd2bin(b[6]); /* 2000..2099 */

    return 0;
}

int pcf8563_set_time(struct pcf8563 *dev, const struct tm *t)
{
    uint8_t b[7];

    /* Seconds: ST=1 to keep oscillator running. */
    b[0] = bin2bcd((uint8_t)t->tm_sec) & 0x7F;
    b[0] |= RTCSEC_ST;

    b[1] = bin2bcd((uint8_t)t->tm_min) & 0x7F;
    b[2] = bin2bcd((uint8_t)t->tm_hour) & 0x3F; /* 24 h */

    /* Weekday: store 1..7, plus VBATEN. */
    uint8_t wd = (uint8_t)((t->tm_wday % 7) + 1); /* 1..7 */
    uint8_t wkday = (bin2bcd(wd) & 0x07) | RTCWKDAY_VBATEN;
    b[3] = wkday;

    b[4] = bin2bcd((uint8_t)t->tm_mday) & 0x3F;

    uint8_t mon = (uint8_t)(t->tm_mon + 1); /* 1..12 */
    b[5] = bin2bcd(mon) & 0x1F;             /* ensure LPYR bit stays 0 on write */

    uint8_t yr = (uint8_t)(t->tm_year % 100);
    b[6] = bin2bcd(yr);

    return wr(dev, REG_RTCSEC, b, sizeof(b));
}

int pcf8563_set_alarm_tm(struct pcf8563 *dev, const struct tm *t)
{
    if (!dev || !t)
    {
        return -EINVAL;
    }

    /* Sanity-ish checks, optional */
    if (t->tm_sec < 0 || t->tm_sec > 59 ||
        t->tm_min < 0 || t->tm_min > 59 ||
        t->tm_hour < 0 || t->tm_hour > 23 ||
        t->tm_mday < 1 || t->tm_mday > 31 ||
        t->tm_mon < 0 || t->tm_mon > 11)
    {
        return -EINVAL;
    }

    uint8_t sec = bin2bcd((uint8_t)t->tm_sec) & 0x7F;
    uint8_t min = bin2bcd((uint8_t)t->tm_min) & 0x7F;
    uint8_t hour = bin2bcd((uint8_t)t->tm_hour) & 0x3F;

    /* Weekday: MCP7940N stores 1..7, tm_wday is 0..6. */
    uint8_t wd = (uint8_t)((t->tm_wday % 7) + 1); /* 1..7 */
    uint8_t wkday = bin2bcd(wd) & 0x07;

    /* Set mask to FULL MATCH: MSK2:0 = 111 (seconds, minutes, hours, etc.) */
    wkday |= (0b111 << 4);

    uint8_t date = bin2bcd((uint8_t)t->tm_mday) & 0x3F;
    uint8_t month = bin2bcd((uint8_t)(t->tm_mon + 1)) & 0x1F; /* 1..12 */

    uint8_t buf[6] = {
        sec,
        min,
        hour,
        wkday,
        date,
        month,
    };

    int rc = wr(dev, REG_ALM0SEC, buf, sizeof(buf));
    if (rc)
    {
        return rc;
    }

    /* Clear any pending flag and enable IRQ. */
    rc = pcf8563_alarm_clear_flag(dev);
    if (rc)
    {
        return rc;
    }

    return pcf8563_alarm_irq_enable(dev, true);
}
