/* pcf8563.h */
#ifndef PCF8563_H
#define PCF8563_H

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h> /* <-- add this for k_work */
#include <time.h>

struct pcf8563
{
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio; /* from int-gpios */
    struct gpio_callback gpio_cb;

    /* run callback out of interrupt context */
    struct k_work work;

    void (*alarm_cb)(void *user);
    void *alarm_user;
};

/* API */
int pcf8563_init(struct pcf8563 *dev);
void pcf8563_set_alarm_callback(struct pcf8563 *dev,
                                void (*cb)(void *user), void *user);

int pcf8563_get_time(struct pcf8563 *dev, struct tm *t_out);
int pcf8563_set_time(struct pcf8563 *dev, const struct tm *t_in);

int pcf8563_set_alarm_hm(struct pcf8563 *dev, int hour, int minute);
int pcf8563_alarm_irq_enable(struct pcf8563 *dev, bool enable);
int pcf8563_alarm_clear_flag(struct pcf8563 *dev);

#endif
