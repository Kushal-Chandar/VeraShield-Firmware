/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 * Note:
 * Tested on nRF Connect SDK Version : 2.0
 */

/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

LOG_MODULE_REGISTER(Lesson4_Exercise2, LOG_LEVEL_INF);

/* Retrieve the device structure for the servo motor */
#define SERVO_MOTOR DT_NODELABEL(servo)
static const struct pwm_dt_spec pwm_servo = PWM_DT_SPEC_GET(SERVO_MOTOR);

/* Use DT_PROP() to obtain the minimum and maximum duty cycle */
#define PWM_SERVO_MIN_PULSE_WIDTH DT_PROP(SERVO_MOTOR, min_pulse)
#define PWM_SERVO_MAX_PULSE_WIDTH DT_PROP(SERVO_MOTOR, max_pulse)
#define PWM_PERIOD DT_PROP(SERVO_MOTOR, period)

/* Create a function to set the angle of the motor */
int set_motor_angle(uint32_t pulse_width_ns)
{
    int err;

    err = pwm_set_dt(&pwm_servo, PWM_PERIOD, pulse_width_ns);
    if (err)
    {
        LOG_ERR("pwm_set_dt_returned %d", err);
    }
    return err;
}

int main(void)
{
    int err = 0;

    /* Check if the motor device is ready and set its initial value */
    LOG_INF("Initializing servo motor");
    if (!pwm_is_ready_dt(&pwm_servo))
    {
        LOG_ERR("Error: PWM device %s is not ready", pwm_servo.dev->name);
        return 0;
    }

    LOG_INF("Servo motor ready. Starting continuous movement...");

    /* Continuous servo movement loop */
    while (1)
    {
        /* Move to minimum position (0 degrees) */
        LOG_INF("Moving to 0 degrees");
        err = set_motor_angle(PWM_SERVO_MIN_PULSE_WIDTH);
        if (err)
        {
            LOG_ERR("Error setting motor angle: %d", err);
        }
        k_sleep(K_MSEC(2000)); // Wait 2 seconds

        /* Move to middle position (90 degrees) */
        LOG_INF("Moving to 90 degrees");
        uint32_t middle_pulse = (PWM_SERVO_MIN_PULSE_WIDTH + PWM_SERVO_MAX_PULSE_WIDTH) / 2;
        err = set_motor_angle(middle_pulse);
        if (err)
        {
            LOG_ERR("Error setting motor angle: %d", err);
        }
        k_sleep(K_MSEC(2000)); // Wait 2 seconds

        /* Move to maximum position (180 degrees) */
        LOG_INF("Moving to 180 degrees");
        err = set_motor_angle(PWM_SERVO_MAX_PULSE_WIDTH);
        if (err)
        {
            LOG_ERR("Error setting motor angle: %d", err);
        }
        k_sleep(K_MSEC(2000)); // Wait 2 seconds

        /* Back to middle position */
        LOG_INF("Returning to 90 degrees");
        err = set_motor_angle(middle_pulse);
        if (err)
        {
            LOG_ERR("Error setting motor angle: %d", err);
        }
        k_sleep(K_MSEC(2000)); // Wait 2 seconds

        LOG_INF("Cycle complete. Starting next cycle...");
        k_sleep(K_MSEC(500)); // Short pause before next cycle
    }

    return 0;
}