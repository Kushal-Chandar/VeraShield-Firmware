#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>
#include "ble.h"
#include "cycle.h"
#include "vbat.h"
#include "spray.h"
#include "slider.h"
#include "pcf8563.h"
#include "tm_helpers.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

#define PCF8563_NODE DT_NODELABEL(pcf8563)

static struct pcf8563 rtc = {
    .i2c = I2C_DT_SPEC_GET(PCF8563_NODE),
    .int_gpio = GPIO_DT_SPEC_GET(PCF8563_NODE, int_gpios),
};

static void seed_time_from_build_if_needed(void)
{
    struct tm now;
    if (pcf8563_get_time(&rtc, &now) == 0 && tm_sane(&now))
        return;

    static const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {0};
    int d, y, H, M, S;
    if (sscanf(__DATE__, "%3s %d %d", m, &d, &y) != 3)
        return;
    if (sscanf(__TIME__, "%d:%d:%d", &H, &M, &S) != 3)
        return;
    const char *p = strstr(mons, m);
    int mon = p ? (int)((p - mons) / 3) : 0;

    struct tm t = {
        .tm_sec = S, .tm_min = M, .tm_hour = H, .tm_mday = d, .tm_mon = mon, .tm_year = y - 1900, .tm_isdst = -1};
    if (pcf8563_set_time(&rtc, &t) == 0)
    {
        LOG_INF("RTC seeded: %04d-%02d-%02d %02d:%02d:%02d",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
    }
}

static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
    (BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY),
    800, 801, NULL);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct gpio_dt_spec status_led =
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0});

#define RUN_LED_BLINK_INTERVAL 1000

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_MACHHAR_SERVICE_VAL),
};

static bool is_connected = false;
static bool is_advertising = false;

static struct k_work adv_work;
static struct k_work_delayable adv_stop_work;

static void advertising_start(void);

static void adv_stop_work_handler(struct k_work *work)
{
    int err = bt_le_adv_stop();
    if (err && err != -EALREADY)
    {
        LOG_ERR("bt_le_adv_stop err %d", err);
        return;
    }
    is_advertising = false;
    LOG_INF("Advertising stopped (timeout)");
}

static void adv_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("bt_le_adv_start err %d", err);
        return;
    }
    is_advertising = true;
    LOG_INF("Advertising started");
    k_work_schedule(&adv_stop_work, K_MINUTES(2));
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }
    is_connected = true;
    is_advertising = false;
    k_work_cancel_delayable(&adv_stop_work);
    gpio_pin_set_dt(&status_led, 1);
    LOG_INF("Connected");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);
    is_connected = false;
    if (!is_advertising)
    {
        advertising_start(); /* restart for another 2 minutes */
    }
}

static void recycled_cb(void)
{
    // Now device needs to restart for advertising again
}

static struct bt_conn_cb connection_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .recycled = recycled_cb,
};

static void advertising_start(void)
{
    k_work_submit(&adv_work);
}

int main(void)
{
    int err;

    LOG_INF("Starting BLE + status LED");

    if (!device_is_ready(status_led.port))
    {
        LOG_ERR("status_led port not ready");
        return -1;
    }
    err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
    if (err)
    {
        LOG_ERR("LED config err %d", err);
        return -1;
    }
    if (!device_is_ready(rtc.i2c.bus))
    {
        printk("I2C bus not ready\n");
        return -1;
    }
    pcf8563_bind(&rtc);
    seed_time_from_build_if_needed();

    cycle_init();
    cycle_tick_start();
    struct cycle_cfg_t init = {.spray_ms = 2000, .idle_ms = 3000, .repeats = 0};
    cycle_set_cfg(&init);

    if (vbat_init() == 0)
        vbat_start();
    if (slider_init() != 0)
        LOG_ERR("slider_init failed");
    if (spray_init() != 0)
        LOG_ERR("spray_init failed");
    spray_callback();

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init err %d", err);
        return -1;
    }
    bt_conn_cb_register(&connection_callbacks);
    LOG_INF("Bluetooth initialized");

    k_work_init(&adv_work, adv_work_handler);
    k_work_init_delayable(&adv_stop_work, adv_stop_work_handler);
    advertising_start();

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
            gpio_pin_set_dt(&status_led, 0);
            k_sleep(K_MSEC(500));
        }
    }

    return 0;
}