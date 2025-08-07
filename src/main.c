/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 * Note:
 * Tested on nRF Connect SDK Version : 2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

/* Sleep time - 10 minutes */
#define SLEEP_TIME_MS 10 * 60 * 1000

/* Long press threshold - 2 seconds */
#define LONG_PRESS_TIME_MS 2000

/* Debounce time - 50ms */
#define DEBOUNCE_TIME_MS 50

/* Button definitions */
#define PW_SW_NODE DT_ALIAS(pw_sw)
#define BLT_SW_NODE DT_ALIAS(blt_sw)

static const struct gpio_dt_spec pw_button = GPIO_DT_SPEC_GET(PW_SW_NODE, gpios);
static const struct gpio_dt_spec blt_button = GPIO_DT_SPEC_GET(BLT_SW_NODE, gpios);

/* LED definitions */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

/* Button state tracking */
struct button_state
{
    bool pressed;
    int64_t press_time;
    bool long_press_handled;
};

static struct button_state pw_button_state = {0};
static struct button_state blt_button_state = {0};

/* Work items for handling button press logic */
static struct k_work pw_button_work;
static struct k_work blt_button_work;

/* Timers for long press detection */
static struct k_timer pw_long_press_timer;
static struct k_timer blt_long_press_timer;

/* Function prototypes */
void pw_button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void blt_button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void pw_button_work_handler(struct k_work *work);
void blt_button_work_handler(struct k_work *work);
void pw_long_press_handler(struct k_timer *timer);
void blt_long_press_handler(struct k_timer *timer);

/* GPIO callbacks */
static struct gpio_callback pw_button_cb_data;
static struct gpio_callback blt_button_cb_data;

/* PW Button Functions */
void pw_button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_work_submit(&pw_button_work);
}

void pw_button_work_handler(struct k_work *work)
{
    int button_val = gpio_pin_get_dt(&pw_button);
    int64_t current_time = k_uptime_get();

    if (button_val == 1)
    { // Button pressed (assuming active high)
        if (!pw_button_state.pressed)
        {
            pw_button_state.pressed = true;
            pw_button_state.press_time = current_time;
            pw_button_state.long_press_handled = false;

            // Start long press timer
            k_timer_start(&pw_long_press_timer, K_MSEC(LONG_PRESS_TIME_MS), K_NO_WAIT);
        }
    }
    else
    { // Button released
        if (pw_button_state.pressed)
        {
            pw_button_state.pressed = false;
            k_timer_stop(&pw_long_press_timer);

            int64_t press_duration = current_time - pw_button_state.press_time;

            if (!pw_button_state.long_press_handled && press_duration < LONG_PRESS_TIME_MS)
            {
                // Short press - toggle LED0
                gpio_pin_toggle_dt(&led0);
                printk("PW Button: Short press - LED0 toggled\n");
            }
        }
    }
}

void pw_long_press_handler(struct k_timer *timer)
{
    if (pw_button_state.pressed && !pw_button_state.long_press_handled)
    {
        // Long press - turn on both LEDs
        gpio_pin_set_dt(&led0, 1);
        gpio_pin_set_dt(&led1, 1);
        pw_button_state.long_press_handled = true;
        printk("PW Button: Long press - Both LEDs turned ON\n");
    }
}

/* BLT Button Functions */
void blt_button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_work_submit(&blt_button_work);
}

void blt_button_work_handler(struct k_work *work)
{
    int button_val = gpio_pin_get_dt(&blt_button);
    int64_t current_time = k_uptime_get();

    if (button_val == 1)
    { // Button pressed (assuming active high)
        if (!blt_button_state.pressed)
        {
            blt_button_state.pressed = true;
            blt_button_state.press_time = current_time;
            blt_button_state.long_press_handled = false;

            // Start long press timer
            k_timer_start(&blt_long_press_timer, K_MSEC(LONG_PRESS_TIME_MS), K_NO_WAIT);
        }
    }
    else
    { // Button released
        if (blt_button_state.pressed)
        {
            blt_button_state.pressed = false;
            k_timer_stop(&blt_long_press_timer);

            int64_t press_duration = current_time - blt_button_state.press_time;

            if (!blt_button_state.long_press_handled && press_duration < LONG_PRESS_TIME_MS)
            {
                // Short press - toggle LED1
                gpio_pin_toggle_dt(&led1);
                printk("BLT Button: Short press - LED1 toggled\n");
            }
        }
    }
}

void blt_long_press_handler(struct k_timer *timer)
{
    if (blt_button_state.pressed && !blt_button_state.long_press_handled)
    {
        // Long press - turn off both LEDs
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led1, 0);
        blt_button_state.long_press_handled = true;
        printk("BLT Button: Long press - Both LEDs turned OFF\n");
    }
}

int main(void)
{
    int ret;

    /* Check if LED devices are ready */
    if (!device_is_ready(led0.port))
    {
        printk("Error: LED0 device not ready\n");
        return -1;
    }

    if (!device_is_ready(led1.port))
    {
        printk("Error: LED1 device not ready\n");
        return -1;
    }

    /* Check if button devices are ready */
    if (!device_is_ready(pw_button.port))
    {
        printk("Error: PW Button device not ready\n");
        return -1;
    }

    if (!device_is_ready(blt_button.port))
    {
        printk("Error: BLT Button device not ready\n");
        return -1;
    }

    /* Configure LEDs as outputs */
    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (ret < 0)
    {
        printk("Error: Failed to configure LED0\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    if (ret < 0)
    {
        printk("Error: Failed to configure LED1\n");
        return -1;
    }

    /* Configure buttons as inputs */
    ret = gpio_pin_configure_dt(&pw_button, GPIO_INPUT);
    if (ret < 0)
    {
        printk("Error: Failed to configure PW Button\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(&blt_button, GPIO_INPUT);
    if (ret < 0)
    {
        printk("Error: Failed to configure BLT Button\n");
        return -1;
    }

    /* Configure interrupts on button pins (both edges to detect press and release) */
    ret = gpio_pin_interrupt_configure_dt(&pw_button, GPIO_INT_EDGE_BOTH);
    if (ret < 0)
    {
        printk("Error: Failed to configure PW Button interrupt\n");
        return -1;
    }

    ret = gpio_pin_interrupt_configure_dt(&blt_button, GPIO_INT_EDGE_BOTH);
    if (ret < 0)
    {
        printk("Error: Failed to configure BLT Button interrupt\n");
        return -1;
    }

    /* Initialize work items */
    k_work_init(&pw_button_work, pw_button_work_handler);
    k_work_init(&blt_button_work, blt_button_work_handler);

    /* Initialize timers */
    k_timer_init(&pw_long_press_timer, pw_long_press_handler, NULL);
    k_timer_init(&blt_long_press_timer, blt_long_press_handler, NULL);

    /* Initialize and add GPIO callbacks */
    gpio_init_callback(&pw_button_cb_data, pw_button_pressed, BIT(pw_button.pin));
    gpio_init_callback(&blt_button_cb_data, blt_button_pressed, BIT(blt_button.pin));

    gpio_add_callback(pw_button.port, &pw_button_cb_data);
    gpio_add_callback(blt_button.port, &blt_button_cb_data);

    printk("Button control system initialized\n");
    printk("PW Button: Short press = toggle LED0, Long press = turn ON both LEDs\n");
    printk("BLT Button: Short press = toggle LED1, Long press = turn OFF both LEDs\n");

    while (1)
    {
        k_msleep(SLEEP_TIME_MS);
    }

    return 0;
}