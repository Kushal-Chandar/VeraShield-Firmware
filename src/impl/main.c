/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/conn.h>
// #include <dk_buttons_and_leds.h>
#include <zephyr/drivers/gpio.h>

#include "ble.h"
#include "vbat.h"
#include "cycle.h"
#include "servo.h" /* if you use it */
#include "slider.h"
#include "manual_spray.h"

#include "slider.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

// #define SLEEP_TIME_MS 10 * 60 * 1000
#define SLEEP_TIME_MS 1000

static int settings_runtime_load(void)
{
#if defined(CONFIG_BT_DIS_SETTINGS)
    settings_runtime_set("bt/dis/model",
                         "Zephyr Model",
                         sizeof("Zephyr Model"));
    settings_runtime_set("bt/dis/manuf",
                         "Zephyr Manufacturer",
                         sizeof("Zephyr Manufacturer"));
#if defined(CONFIG_BT_DIS_SERIAL_NUMBER)
    settings_runtime_set("bt/dis/serial",
                         CONFIG_BT_DIS_SERIAL_NUMBER_STR,
                         sizeof(CONFIG_BT_DIS_SERIAL_NUMBER_STR));
#endif
#if defined(CONFIG_BT_DIS_SW_REV)
    settings_runtime_set("bt/dis/sw",
                         CONFIG_BT_DIS_SW_REV_STR,
                         sizeof(CONFIG_BT_DIS_SW_REV_STR));
#endif
#if defined(CONFIG_BT_DIS_FW_REV)
    settings_runtime_set("bt/dis/fw",
                         CONFIG_BT_DIS_FW_REV_STR,
                         sizeof(CONFIG_BT_DIS_FW_REV_STR));
#endif
#if defined(CONFIG_BT_DIS_HW_REV)
    settings_runtime_set("bt/dis/hw",
                         CONFIG_BT_DIS_HW_REV_STR,
                         sizeof(CONFIG_BT_DIS_HW_REV_STR));
#endif
#endif
    return 0;
}

int main(void)
{
    int blink_status = 0;
    int err;

    cycle_init();
    cycle_tick_start();

    /* Optional initial config (will be replaced on first button press) */
    struct cycle_cfg_t init = {.spray_ms = 2000, .idle_ms = 3000, .repeats = 0};
    cycle_set_cfg(&init);

    // settings_runtime_load();

    if (vbat_init() == 0)
        vbat_start();

    if (slider_init() != 0)
        LOG_ERR("slider_init failed");

    if (manual_spray_init() != 0)
        LOG_ERR("manual_spray_init failed");

    manual_spray_callback();

    for (;;)
    {
        LOG_INF("f %d", slider_read_millivolts());
        k_sleep(K_MSEC(SLEEP_TIME_MS));
    }
}