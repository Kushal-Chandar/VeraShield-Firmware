#include "pcf8563.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pcf8563, LOG_LEVEL_INF);

/* Registers (datasheet) */
#define REG_CTRL1 0x00
#define REG_CTRL2 0x01
#define REG_SECONDS 0x02
#define REG_MINUTES 0x03
#define REG_HOURS 0x04
#define REG_DAYS 0x05
#define REG_WEEKDAYS 0x06
#define REG_MONTHS 0x07
#define REG_YEARS 0x08
#define REG_MINUTE_ALARM 0x09
#define REG_HOUR_ALARM 0x0A
#define REG_DAY_ALARM 0x0B
#define REG_WEEKDAY_ALARM 0x0C

/* CTRL1 bits */
#define CTRL1_STOP BIT(5) /* stop oscillator when 1 */

/* CTRL2 bits */
#define CTRL2_TIE BIT(0)
#define CTRL2_AIE BIT(1)
#define CTRL2_TF BIT(2)
#define CTRL2_AF BIT(3)

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

/* ---- IRQ deferral: work item bound to the single instance ---- */
struct pcf8563_work_ctx
{
    struct k_work work;
    struct pcf8563 *dev;
};
static struct pcf8563_work_ctx s_wctx;

static void pcf8563_work_handler(struct k_work *work)
{
    struct pcf8563_work_ctx *ctx = CONTAINER_OF(work, struct pcf8563_work_ctx, work);
    struct pcf8563 *dev = ctx->dev;

    /* Clear AF/TF in thread context so INT releases */
    (void)rd(dev, REG_CTRL2, (uint8_t[1]){0}, 0); /* no-op read to satisfy static analysis */
    (void)pcf8563_alarm_clear_flag(dev);

    /* Call user callback */
    if (dev->alarm_cb)
    {
        dev->alarm_cb(dev->alarm_user);
    }
}

/* ISR: DO NOT touch I2C here. Just schedule work. */
static void pcf8563_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(pins);
    (void)k_work_submit(&s_wctx.work);
}

/* Public API */
int pcf8563_alarm_clear_flag(struct pcf8563 *dev)
{
    uint8_t c2;
    int rc = rd(dev, REG_CTRL2, &c2, 1);
    if (rc)
        return rc;
    c2 &= ~(CTRL2_AF | CTRL2_TF);
    return wr8(dev, REG_CTRL2, c2);
}

int pcf8563_alarm_irq_enable(struct pcf8563 *dev, bool enable)
{
    uint8_t c2;
    int rc = rd(dev, REG_CTRL2, &c2, 1);
    if (rc)
        return rc;
    if (enable)
        c2 |= CTRL2_AIE;
    else
        c2 &= ~CTRL2_AIE;
    return wr8(dev, REG_CTRL2, c2);
}

int pcf8563_init(struct pcf8563 *dev)
{
    /* 1) I2C ready */
    if (!device_is_ready(dev->i2c.bus))
    {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /* 2) Make sure oscillator is running (clear STOP) */
    uint8_t c1;
    if (rd(dev, REG_CTRL1, &c1, 1) == 0)
    {
        if (c1 & CTRL1_STOP)
        {
            c1 &= ~CTRL1_STOP;
            (void)wr8(dev, REG_CTRL1, c1);
        }
    }

    /* 3) INT GPIO from DT (open-drain active-low → falling edge) */
    if (!device_is_ready(dev->int_gpio.port))
    {
        LOG_ERR("INT GPIO port not ready");
        return -ENODEV;
    }
    int rc = gpio_pin_configure_dt(&dev->int_gpio, GPIO_INPUT);
    if (rc)
        return rc;

    rc = gpio_pin_interrupt_configure_dt(&dev->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    if (rc)
        return rc;

    gpio_init_callback(&dev->gpio_cb, pcf8563_isr, BIT(dev->int_gpio.pin));
    gpio_add_callback(dev->int_gpio.port, &dev->gpio_cb);

    /* 4) Clear stale flags and disable AIE/TIE initially */
    uint8_t c2 = 0;
    (void)rd(dev, REG_CTRL2, &c2, 1);
    c2 &= ~(CTRL2_AF | CTRL2_TF | CTRL2_AIE | CTRL2_TIE);
    (void)wr8(dev, REG_CTRL2, c2);

    /* 5) Bind and init work context */
    s_wctx.dev = dev;
    k_work_init(&s_wctx.work, pcf8563_work_handler);

    LOG_INF("PCF8563 init ok (INT on %s.%u)", dev->int_gpio.port->name, dev->int_gpio.pin);
    return 0;
}

void pcf8563_set_alarm_callback(struct pcf8563 *dev,
                                void (*cb)(void *user), void *user)
{
    dev->alarm_cb = cb;
    dev->alarm_user = user;
}

int pcf8563_get_time(struct pcf8563 *dev, struct tm *t)
{
    uint8_t b[7];
    int rc = rd(dev, REG_SECONDS, b, sizeof(b));
    if (rc)
        return rc;

    t->tm_sec = bcd2bin(b[0] & 0x7F);
    t->tm_min = bcd2bin(b[1] & 0x7F);
    t->tm_hour = bcd2bin(b[2] & 0x3F);
    t->tm_mday = bcd2bin(b[3] & 0x3F);
    t->tm_wday = bcd2bin(b[4] & 0x07);
    t->tm_mon = bcd2bin(b[5] & 0x1F) - 1;
    t->tm_year = 100 + bcd2bin(b[6]); /* assume 2000..2099 */

    return 0;
}

int pcf8563_set_time(struct pcf8563 *dev, const struct tm *t)
{
    uint8_t b[7];
    b[0] = bin2bcd((uint8_t)t->tm_sec) & 0x7F;
    b[1] = bin2bcd((uint8_t)t->tm_min) & 0x7F;
    b[2] = bin2bcd((uint8_t)t->tm_hour) & 0x3F;
    b[3] = bin2bcd((uint8_t)t->tm_mday) & 0x3F;
    b[4] = bin2bcd((uint8_t)t->tm_wday) & 0x07;
    b[5] = bin2bcd((uint8_t)(t->tm_mon + 1)) & 0x1F;
    b[6] = bin2bcd((uint8_t)((t->tm_year + 1900) % 100));
    return wr(dev, REG_SECONDS, b, sizeof(b));
}

int pcf8563_set_alarm_hm(struct pcf8563 *dev, int hour, int minute)
{
    /* AE bit (bit7) = 1 means ignore field */
    uint8_t a_min = (minute >= 0) ? (bin2bcd((uint8_t)minute) & 0x7F) : 0x80;
    uint8_t a_hour = (hour >= 0) ? (bin2bcd((uint8_t)hour) & 0x3F) : 0x80;
    uint8_t a_day = 0x80;  /* ignore */
    uint8_t a_wday = 0x80; /* ignore */

    int rc = wr(dev, REG_MINUTE_ALARM, (uint8_t[]){a_min, a_hour, a_day, a_wday}, 4);
    if (rc)
        return rc;

    /* Clear AF, then enable AIE so INT can assert on match */
    (void)pcf8563_alarm_clear_flag(dev);
    return pcf8563_alarm_irq_enable(dev, true);
}
