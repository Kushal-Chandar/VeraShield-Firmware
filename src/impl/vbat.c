#include "vbat.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(VBAT, LOG_LEVEL_INF);

/* ADC: /zephyr,user named "vbat" */
static const struct adc_dt_spec adc_vbat =
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), vbat);

/* LED: alias led3 */
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);

static struct k_work_delayable blink_work;
// static struct k_work_delayable sample_work;

static volatile int last_mv = -1;
static uint32_t blink_period_ms = 1000;
static bool running = false;
static bool led_on = false;

/* tweak for your battery */
#define VBAT_GREEN_MV 2600
#define VBAT_YELLOW_MV 2000
#define VBAT_RED_MV 1500

static uint32_t decide_period_ms(int mv)
{
    if (mv < 0)
        return 1000;
    if (mv >= VBAT_GREEN_MV)
        return 1000; /* 1 Hz */
    if (mv >= VBAT_YELLOW_MV)
        return 500; /* 2 Hz */
    if (mv >= VBAT_RED_MV)
        return 250; /* 4 Hz */
    return 125;     /* 8 Hz critical */
}

static void blink_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    if (!running)
        return;

    led_on = !led_on;
    gpio_pin_set_dt(&led3, led_on);
    k_work_schedule(&blink_work, K_MSEC(blink_period_ms / 2U));
}

static void sample_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    if (!running)
        return;

    int err;
    int16_t raw;
    struct adc_sequence seq = {
        .buffer = &raw,
        .buffer_size = sizeof(raw),
    };

    err = adc_sequence_init_dt(&adc_vbat, &seq);
    if (!err)
        err = adc_read(adc_vbat.dev, &seq);

    if (!err)
    {
        int mv = (int)raw;
        if (adc_raw_to_millivolts_dt(&adc_vbat, &mv) < 0)
        {
            /* mv stays raw if conversion not available */
            LOG_WRN("Conversion not available");
        }
        last_mv = mv;
        blink_period_ms = decide_period_ms(mv);
        LOG_INF("raw=%d mv=%d -> blink %ums", raw, mv, blink_period_ms);
    }
    else
    {
        LOG_ERR("adc read err=%d", err);
    }

    // k_work_schedule(&sample_work, K_SECONDS(1));
}

int vbat_init(void)
{
    int err;

    if (!adc_is_ready_dt(&adc_vbat))
    {
        LOG_ERR("ADC %s not ready", adc_vbat.dev->name);
        return -ENODEV;
    }
    err = adc_channel_setup_dt(&adc_vbat);
    if (err)
    {
        LOG_ERR("adc_channel_setup_dt: %d", err);
        return err;
    }

    if (!device_is_ready(led3.port))
    {
        LOG_ERR("led3 GPIO not ready");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);

    k_work_init_delayable(&blink_work, blink_fn);
    // k_work_init_delayable(&sample_work, sample_fn);
    return 0;
}

void vbat_start(void)
{
    if (running)
        return;
    running = true;
    led_on = false;
    gpio_pin_set_dt(&led3, 0);
    // k_work_schedule(&sample_work, K_NO_WAIT);
    k_work_schedule(&blink_work, K_MSEC(blink_period_ms / 2U));
}

void vbat_stop(void)
{
    running = false;
    // k_work_cancel_delayable(&sample_work);
    k_work_cancel_delayable(&blink_work);
    gpio_pin_set_dt(&led3, 0);
}

int vbat_last_millivolts(void) { return last_mv; }
