/* mcp7940n.h - API kept as-is, implementation now targets MCP7940N */
#ifndef MCP7940N_H
#define MCP7940N_H

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <time.h>

struct mcp7940n
{
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
    struct gpio_callback gpio_cb;

    struct k_work work;

    void (*alarm_cb)(void *user);
    void *alarm_user;
};

void mcp7940n_bind(struct mcp7940n *dev);
struct mcp7940n *mcp7940n_get(void);

int mcp7940n_init(struct mcp7940n *dev);
void mcp7940n_set_alarm_callback(struct mcp7940n *dev,
                                 void (*cb)(void *user), void *user);

int mcp7940n_get_time(struct mcp7940n *dev, struct tm *t_out);
int mcp7940n_set_time(struct mcp7940n *dev, const struct tm *t_in);

int mcp7940n_alarm_irq_enable(struct mcp7940n *dev, bool enable);
int mcp7940n_alarm_clear_flag(struct mcp7940n *dev);
int mcp7940n_set_alarm_tm(struct mcp7940n *dev, const struct tm *t);

#endif
