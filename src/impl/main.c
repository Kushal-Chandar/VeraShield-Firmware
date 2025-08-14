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
#include <dk_buttons_and_leds.h>

#include "ble.h"
#include "vbat.h"
#include "cycle.h"
#include "servo.h" /* if you use it */

#include "slider.h"

LOG_MODULE_REGISTER(Lesson5_Exercise1, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED DK_LED1
#define CON_STATUS_LED DK_LED2
#define USER_LED DK_LED3
#define USER_BUTTON DK_BTN1_MSK

#define RUN_LED_BLINK_INTERVAL 1000

static bool app_button_state;
static struct k_work adv_work;
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};

static void adv_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    if (err)
    {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Advertising successfully started\n");
}

static void advertising_start(void)
{
    k_work_submit(&adv_work);
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_INF("Connection failed (err %u)\n", err);
        return;
    }

    dk_set_led_on(CON_STATUS_LED);
    LOG_INF("Connected\n");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)\n", reason);

    dk_set_led_off(CON_STATUS_LED);
}

static void recycled_cb(void)
{
    printk("Connection object available from previous conn. Disconnect is complete!\n");
    advertising_start();
}

/* STEP 5.2 Define the callback function security_changed() */
static void on_security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    // char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_t addr;
    size_t count = 1;
    bt_id_get(&addr, &count);
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));
    LOG_INF("Using hardware Bluetooth address: %s", addr_str);

    // bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err)
    {
        LOG_INF("Security changed: %s level %u\n", addr_str, level);
    }
    else
    {
        LOG_INF("Security failed: %s level %u err %d\n", addr_str, level, err);
    }
}
struct bt_conn_cb connection_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .recycled = recycled_cb,
    /* STEP 5.1 - Add the security_changed member to the callback structure */
    .security_changed = on_security_changed,
};

static void app_led_cb(bool led_state)
{
    dk_set_led(USER_LED, led_state);
}

static bool app_button_cb(void)
{
    return app_button_state;
}

static struct bt_lbs_cb lbs_callbacs = {
    .led_cb = app_led_cb,
    .button_cb = app_button_cb,
};

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
    if (has_changed & USER_BUTTON)
    {
        uint32_t user_button_state = button_state & USER_BUTTON;

        bt_lbs_send_button_state(user_button_state);
        app_button_state = user_button_state ? true : false;
    }
}

static int init_button(void)
{
    int err;

    err = dk_buttons_init(button_changed);
    if (err)
    {
        LOG_INF("Cannot init buttons (err: %d)\n", err);
    }

    return err;
}
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

    LOG_INF("Starting Lesson 5 - Exercise 1 \n");

    // if (IS_ENABLED(CONFIG_BT_SETTINGS))
    // {
    //     settings_load();
    // }
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

    err = dk_leds_init();
    if (err)
    {
        LOG_INF("LEDs init failed (err %d)\n", err);
        return -1;
    }

    err = init_button();
    if (err)
    {
        LOG_INF("Button init failed (err %d)\n", err);
        return -1;
    }

    // /* STEP 10 - Register the authentication callbacks */
    // err = bt_conn_auth_cb_register(&conn_auth_callbacks);
    // if (err)
    // {
    //     LOG_INF("Failed to register authorization callbacks\n");
    //     return -1;
    // }

    bt_conn_cb_register(&connection_callbacks);

    err = bt_enable(NULL);
    if (err)
    {
        LOG_INF("Bluetooth init failed (err %d)\n", err);
        return -1;
    }

    err = bt_lbs_init(&lbs_callbacs);
    if (err)
    {
        LOG_INF("Failed to init LBS (err:%d)\n", err);
        return -1;
    }

    LOG_INF("Bluetooth initialized\n");

    k_work_init(&adv_work, adv_work_handler);
    advertising_start();

    for (;;)
    {
        dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
        k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
    }
}