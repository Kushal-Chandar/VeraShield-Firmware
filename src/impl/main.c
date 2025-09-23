#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>
#include "ble.h"
#include "cycle.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

/* ==== Advertising params: connectable + identity ==== */
static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
    (BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY),
    800, /* 500 ms */
    801, /* 500.625 ms */
    NULL /* undirected */
);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* ==== LED from devicetree (alias: led2). Change alias if your board uses led0/led1/etc. ==== */
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0});

/* Blink timing */
#define RUN_LED_BLINK_INTERVAL 1000 /* ms */

/* Advertising data: flags + device name */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* Scan response: advertise your custom 128-bit Service UUID */
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_MACHHAR_SERVICE_VAL),
};

/* --- simple state (no atomics/dk) --- */
static bool is_connected = false;
static bool is_advertising = false;

/* Work item to start advertising */
static struct k_work adv_work;

static void adv_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }
    is_advertising = true;
    LOG_INF("Advertising started");
}

static void advertising_start(void)
{
    k_work_submit(&adv_work);
}

/* Connection callbacks */
static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }
    is_connected = true;
    is_advertising = false;
    gpio_pin_set_dt(&status_led, 1); /* solid while connected */
    LOG_INF("Connected");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);
    is_connected = false;
    /* Keep LED handled by main loop (it will blink again once adv restarts) */
}

static void recycled_cb(void)
{
    LOG_INF("Conn object recycled; restarting advertising");
    advertising_start();
}

static struct bt_conn_cb connection_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .recycled = recycled_cb,
};

int main(void)
{
    int err;

    LOG_INF("Starting minimal BLE + status LED\n");

    /* Init GPIO LED */
    if (!device_is_ready(status_led.port))
    {
        LOG_ERR("status_led port not ready");
        return -1;
    }
    err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
    if (err)
    {
        LOG_ERR("Failed to configure status LED (err %d)", err);
        return -1;
    }

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

    /* Init BT */
    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return -1;
    }
    bt_conn_cb_register(&connection_callbacks);
    LOG_INF("Bluetooth initialized");

    /* Start advertising via work item */
    k_work_init(&adv_work, adv_work_handler);
    advertising_start();

    /* Blink while advertising; solid while connected */
    bool led_on = false;
    while (1)
    {
        if (is_connected)
        {
            gpio_pin_set_dt(&status_led, 1);
            k_sleep(K_MSEC(500));
        }
        else if (is_advertising)
        {
            led_on = !led_on;
            gpio_pin_set_dt(&status_led, led_on);
            k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
        }
        else
        {
            /* Not connected and not advertising (e.g., before start or on error) */
            gpio_pin_set_dt(&status_led, 0);
            k_sleep(K_MSEC(500));
        }
    }
}
