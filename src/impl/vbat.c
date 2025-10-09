/* vbat.c — drop-in replacement (no blinking) */

#include "vbat.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/services/bas.h>

#include "led_ctrl.h" /* for led_red_set(), led_green_set(), led_blue_set() */

LOG_MODULE_REGISTER(VBAT, LOG_LEVEL_INF);

/* ADC: /zephyr,user named "vbat" */
static const struct adc_dt_spec adc_vbat =
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), vbat);

static struct k_work_delayable sample_work;

static volatile int last_mv = -1;
static volatile uint8_t battery_percent = 0;
static bool running = false;

/* declare if decide_period_ms() is provided elsewhere */
extern uint32_t decide_period_ms(int mv);

/* === Voltage bands (mV) === */
#define VBAT_FULL_MV 8400
#define VBAT_EMPTY_MV 6000
#define VBAT_GREEN_MV 7700
#define VBAT_YELLOW_MV 7000
#define VBAT_RED_MV 6600

/* Actual sampling interval */
#define ADC_SAMPLE_INTERVAL_MS (5 * 60 * 1000)

/* ----- utilities ----- */

static uint8_t voltage_to_percent(int mv)
{
    if (mv <= VBAT_EMPTY_MV)
        return 0;
    if (mv >= VBAT_FULL_MV)
        return 100;

    int range = VBAT_FULL_MV - VBAT_EMPTY_MV; /* 2400 mV span */
    int value = mv - VBAT_EMPTY_MV;
    return (uint8_t)((value * 100) / range);
}

static inline int adc_to_mv(int adc)
{
    return (2353 * adc) / 1000 - 118;
}

/* ----- steady color helpers ----- */

static void set_off(void)
{
    led_red_set(false);
    led_green_set(false);
    led_blue_set(false);
}

static void set_green(void)
{
    led_red_set(false);
    led_green_set(true);
    led_blue_set(false);
}

static void set_yellow(void)
{
    /* Simulate yellow = red + green on */
    led_red_set(true);
    led_green_set(true);
    led_blue_set(false);
}

static void set_red(void)
{
    led_red_set(true);
    led_green_set(false);
    led_blue_set(false);
}

/* ----- apply LED policy based on voltage ----- */

static void apply_leds_for_voltage(int mv)
{
    if (mv >= VBAT_GREEN_MV)
    {
        set_green();
    }
    else if (mv >= VBAT_YELLOW_MV)
    {
        set_yellow();
    }
    else if (mv >= VBAT_RED_MV)
    {
        set_red();
    }
    else
    {
        set_red(); /* below red threshold stays red */
    }
}

/* ----- sampling worker ----- */

static void sample_fn(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!running)
    {
        LOG_INF("ADC sampling stopped - system not running");
        return;
    }

    int err;
    int16_t raw;

    struct adc_sequence seq = {
        .buffer = &raw,
        .buffer_size = sizeof(raw),
    };

    LOG_INF("Reading battery voltage...");

    err = adc_sequence_init_dt(&adc_vbat, &seq);
    if (!err)
    {
        err = adc_read(adc_vbat.dev, &seq);
    }

    if (!err)
    {
        int mv = adc_to_mv((int)raw);

        last_mv = mv;
        battery_percent = voltage_to_percent(mv);

        LOG_INF("Battery: raw=%d mv=%d percent=%d%%",
                raw, mv, battery_percent);

        apply_leds_for_voltage(mv);

        err = bt_bas_set_battery_level(battery_percent);
        if (err)
        {
            LOG_WRN("Failed to update BAS: %d", err);
        }
        else
        {
            LOG_INF("BAS updated: %d%%", battery_percent);
        }
    }
    else
    {
        LOG_ERR("ADC read failed: %d", err);
    }

    k_work_schedule(&sample_work, K_MSEC(ADC_SAMPLE_INTERVAL_MS));
    LOG_INF("Next battery reading scheduled in %u ms",
            (unsigned)ADC_SAMPLE_INTERVAL_MS);
}

/* ----- lifecycle ----- */

int vbat_init(void)
{
    int err;

    LOG_INF("Initializing battery monitoring...");

    if (!adc_is_ready_dt(&adc_vbat))
    {
        LOG_ERR("ADC %s not ready", adc_vbat.dev->name);
        return -ENODEV;
    }

    err = adc_channel_setup_dt(&adc_vbat);
    if (err)
    {
        LOG_ERR("adc_channel_setup_dt failed: %d", err);
        return err;
    }
    LOG_INF("ADC channel configured");

    k_work_init_delayable(&sample_work, sample_fn);

    LOG_INF("Battery monitoring initialization complete");
    return 0;
}

void vbat_start(void)
{
    if (running)
    {
        LOG_INF("Battery monitoring already running");
        return;
    }

    LOG_INF("Starting battery monitoring...");
    running = true;

    set_off();

    k_work_schedule(&sample_work, K_NO_WAIT);
    LOG_INF("Battery monitoring started - first reading immediate, then every %u ms",
            (unsigned)ADC_SAMPLE_INTERVAL_MS);
}

void vbat_stop(void)
{
    if (!running)
    {
        LOG_INF("Battery monitoring already stopped");
        return;
    }

    LOG_INF("Stopping battery monitoring...");
    running = false;

    k_work_cancel_delayable(&sample_work);

    set_off();

    LOG_INF("Battery monitoring stopped");
}

/* ----- getters / triggers ----- */

int vbat_last_millivolts(void)
{
    return last_mv;
}

uint8_t vbat_last_percent(void)
{
    return battery_percent;
}

void vbat_read_now(void)
{
    if (!running)
    {
        LOG_WRN("Cannot read now - system not running");
        return;
    }

    LOG_INF("Forcing immediate battery reading...");
    k_work_schedule(&sample_work, K_NO_WAIT);
}
