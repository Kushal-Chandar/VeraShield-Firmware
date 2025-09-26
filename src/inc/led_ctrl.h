#ifndef LED_CTRL_H
#define LED_CTRL_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <stdbool.h>
#include <stdint.h>

/* Logical LEDs mapped to TLC5916 outputs */
typedef enum
{
    LED_RED = 0,   /* OUT0 -> R_LED   */
    LED_GREEN = 1, /* OUT1 -> G_LED   */
    LED_BLUE = 2,  /* OUT2 -> B_LED   */
    LED_BLT = 3,   /* OUT3 -> BLT_LED */
    LED_PW = 4,    /* OUT4 -> PW_LED  */
    // to change for new pcb
    LED_SPR = 5, /* OUT5 -> SPR_LED */
    /* OUT6/OUT7 unused */
} led_id_t;

/* Init SPI and control GPIOs (LE, OE). Leaves outputs disabled (OE=1), LE=0. */
int led_ctrl_init(void);

/* Enable/disable outputs (OE is active-low). true => outputs on. */
void led_ctrl_enable(bool enable);

/* Write full 8-bit pattern to TLC5916 (bit=1 turns channel on). */
int led_ctrl_write(uint8_t value);

/* Read the last written (shadow) value. */
uint8_t led_ctrl_read_shadow(void);

/* Per-LED control (on/off/toggle) */
int led_ctrl_set(led_id_t id, bool on);
int led_ctrl_toggle(led_id_t id);

/* Convenience helpers */
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

/* All on/off */
int led_ctrl_all_on(void);
int led_ctrl_all_off(void);

#endif /* LED_CTRL_H */
