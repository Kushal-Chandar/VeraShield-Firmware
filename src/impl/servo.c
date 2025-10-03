#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <stdint.h>

#include "servo.h"

LOG_MODULE_REGISTER(PWM_SERVO, LOG_LEVEL_INF);

#define SERVO_NODE DT_NODELABEL(servo)
static const struct pwm_dt_spec pwm_servo = PWM_DT_SPEC_GET(SERVO_NODE);

#define PERIOD DT_PROP(SERVO_NODE, period)
#define MIN_PULSE_WIDTH DT_PROP(SERVO_NODE, min_pulse)
#define MAX_PULSE_WIDTH DT_PROP(SERVO_NODE, max_pulse)

static uint16_t s_angle = 20;

static inline uint32_t angle_to_ns(uint16_t deg)
{
    if (deg > 180)
        deg = 180;
    const uint32_t span = MAX_PULSE_WIDTH - MIN_PULSE_WIDTH;
    return MIN_PULSE_WIDTH + (span * (uint32_t)deg) / 180U;
}

int servo_init(void)
{
    if (!pwm_is_ready_dt(&pwm_servo))
    {
        LOG_ERR("Error: PWM device %s is not ready", pwm_servo.dev->name);
        return -ENODEV;
    }
    servo_set_deg(s_angle);
    return 0;
}

void servo_set_deg(uint16_t deg)
{
    s_angle = (deg > 180) ? 180 : deg;
    int err = pwm_set_dt(&pwm_servo, PERIOD, angle_to_ns(s_angle));
    if (err)
        LOG_ERR("Error setting motor angle: %d", err);
}

int servo_disable(void)
{
    return pwm_set_dt(&pwm_servo, PERIOD, 0);
}

uint16_t servo_get_deg(void) { return s_angle; }