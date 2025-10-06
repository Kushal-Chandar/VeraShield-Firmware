#include "slider.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(SLIDER, LOG_LEVEL_INF);

static const struct adc_dt_spec adc_slider =
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), slider);

enum slider_state
{
    SL_LOW = 1,
    SL_MID = 2,
    SL_HIGH = 3
};
static enum slider_state last_state = SL_LOW;

/* Hysteresis (mV) — tune to your hardware */
#define SL_MID_ENTER_MV 1900
#define SL_MID_EXIT_MV 1750
#define SL_HIGH_ENTER_MV 2400
#define SL_HIGH_EXIT_MV 2250

int slider_init(void)
{
    if (!adc_is_ready_dt(&adc_slider))
    {
        LOG_ERR("ADC %s not ready", adc_slider.dev->name);
        return -ENODEV;
    }
    int err = adc_channel_setup_dt(&adc_slider);
    if (err)
    {
        LOG_ERR("adc_channel_setup_dt: %d", err);
        return err;
    }
    return 0;
}

int slider_read_millivolts(void)
{
    int err;
    int16_t raw;
    struct adc_sequence seq = {
        .buffer = &raw,
        .buffer_size = sizeof(raw),
    };
    err = adc_sequence_init_dt(&adc_slider, &seq);
    if (err)
    {
        LOG_ERR("adc_sequence_init_dt: %d", err);
        return err;
    }
    err = adc_read(adc_slider.dev, &seq);
    if (err)
    {
        LOG_ERR("slider adc_read: %d", err);
        return err;
    }
    int mv = (int)raw;
    err = adc_raw_to_millivolts_dt(&adc_slider, &mv);
    if (err < 0)
    {
        LOG_ERR("adc_raw_to_millivolts_dt: %d", err);
        return err;
    }
    return mv;
}

int slider_classify_from_mv(int mv)
{
    enum slider_state st = last_state;

    switch (st)
    {
    case SL_LOW:
        if (mv >= SL_HIGH_ENTER_MV)
            st = SL_HIGH;
        else if (mv >= SL_MID_ENTER_MV)
            st = SL_MID;
        break;
    case SL_MID:
        if (mv >= SL_HIGH_ENTER_MV)
            st = SL_HIGH;
        else if (mv < SL_MID_EXIT_MV)
            st = SL_LOW;
        break;
    case SL_HIGH:
        if (mv < SL_HIGH_EXIT_MV)
        {
            st = (mv < SL_MID_EXIT_MV) ? SL_LOW : SL_MID;
        }
        break;
    }

    last_state = st;
    LOG_INF("mv=%d -> state %d", mv, (int)st);
    return (int)st;
}

void slider_state_to_cycle_cfg(int state, struct cycle_cfg_t *cfg_out)
{
    switch (state)
    {
    case SL_HIGH:
        *cfg_out = (struct cycle_cfg_t){.spray_ms = 10000, .idle_ms = 2000, .repeats = 5};
        break;
    case SL_MID:
        *cfg_out = (struct cycle_cfg_t){.spray_ms = 7000, .idle_ms = 2000, .repeats = 5};
        break;
    case SL_LOW:
    default:
        *cfg_out = (struct cycle_cfg_t){.spray_ms = 5000, .idle_ms = 2000, .repeats = 5};
        break;
    }
}
