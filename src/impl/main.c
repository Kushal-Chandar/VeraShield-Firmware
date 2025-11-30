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
#include "mcp7940n.h"
#include "tm_helpers.h"
#include "stats.h"
#include "schedule_queue.h"
#include "schedule.h"
#include "led_ctrl.h"
#include "at24c32.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

#define MCP7940N_NODE DT_NODELABEL(mcp7940n)

static struct mcp7940n rtc = {
    .i2c = I2C_DT_SPEC_GET(MCP7940N_NODE),
    .int_gpio = GPIO_DT_SPEC_GET(MCP7940N_NODE, int_gpios),
};

static void seed_time_from_build_if_needed(void)
{
    struct tm now;
    if (mcp7940n_get_time(&rtc, &now) == 0 && tm_sane(&now))
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
    if (mcp7940n_set_time(&rtc, &t) == 0)
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
    led_blt_set(true);
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

static void motor_action(uint8_t intensity, const struct tm *when)
{
    (void)when;
    // motor_start_with_intensity(intensity);
    ble_spray_caller(intensity);
}

static void rtc_alarm_cb(void *user)
{
    (void)user;
    (void)schedule_queue_on_alarm(motor_action);
}

int main(void)
{
    int err;

    err = led_ctrl_init();
    if (err)
    {
        printk("led_ctrl_init() failed: %d\n", err);
        return err;
    }

    LOG_INF("BOOT");

    mcp7940n_init(&rtc);
    mcp7940n_bind(&rtc);
    mcp7940n_set_alarm_callback(&rtc, rtc_alarm_cb, NULL);
    at24c32_init();
    stats_init_if_blank();
    sched_init_if_blank();
    schedule_queue_init_if_blank();
    seed_time_from_build_if_needed();

    (void)schedule_queue_sync_and_arm_next();

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

    while (1)
    {
        if (is_connected)
        {
            led_blt_set(true);
            // k_sleep(K_MSEC(500));
            k_sleep(K_MSEC(5000));
            char tsbuf[100];
            struct tm t;
            mcp7940n_get_time(&rtc, &t);
            LOG_INF("RTC: %s", tm_to_str(&t, tsbuf, sizeof(tsbuf)));
        }
        else if (is_advertising)
        {

            led_blt_toggle();
            k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
        }
        else
        {
            led_blt_set(false);
            k_sleep(K_MSEC(500));
        }
    }

    return 0;
}