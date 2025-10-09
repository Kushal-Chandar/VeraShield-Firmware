#ifndef LED_CTRL_H
#define LED_CTRL_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    LED_RED = 0,   /* OUT0 -> R_LED   */
    LED_GREEN = 1, /* OUT1 -> G_LED   */
    LED_BLUE = 2,  /* OUT2 -> B_LED   */
    LED_BLT = 3,   /* OUT3 -> BLT_LED */
    LED_SPR = 4,   /* OUT4 -> PW_LED  */
} led_id_t;

int led_ctrl_init(void);

void led_ctrl_enable(bool enable);

int led_ctrl_write(uint8_t value);

uint8_t led_ctrl_read_shadow(void);

int led_ctrl_set(led_id_t id, bool on);
int led_ctrl_toggle(led_id_t id);

static inline int led_red_set(bool on) { return led_ctrl_set(LED_RED, on); }
static inline int led_green_set(bool on) { return led_ctrl_set(LED_GREEN, on); }
static inline int led_blue_set(bool on) { return led_ctrl_set(LED_BLUE, on); }
static inline int led_blt_set(bool on) { return led_ctrl_set(LED_BLT, on); }
static inline int led_spray_set(bool on) { return led_ctrl_set(LED_SPR, on); }

static inline int led_red_toggle(void) { return led_ctrl_toggle(LED_RED); }
static inline int led_green_toggle(void) { return led_ctrl_toggle(LED_GREEN); }
static inline int led_blue_toggle(void) { return led_ctrl_toggle(LED_BLUE); }
static inline int led_blt_toggle(void) { return led_ctrl_toggle(LED_BLT); }
static inline int led_spray_toggle(void) { return led_ctrl_toggle(LED_SPR); }

int led_ctrl_all_on(void);
int led_ctrl_all_off(void);

#endif /* LED_CTRL_H */
