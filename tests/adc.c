/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

/* STEP 3.2 - Define variables for both channels */
static const struct adc_dt_spec adc_channel4 = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), vbat);
static const struct adc_dt_spec adc_channel5 = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), slider);

LOG_MODULE_REGISTER(Lesson6_Exercise1, LOG_LEVEL_DBG);

int main(void)
{
    int err;
    uint32_t count = 0;

    /* STEP 4.1 - Define buffers and sequences for both channels */
    int16_t buf0, buf1;

    struct adc_sequence sequence0 = {
        .buffer = &buf0,
        .buffer_size = sizeof(buf0),
    };

    struct adc_sequence sequence1 = {
        .buffer = &buf1,
        .buffer_size = sizeof(buf1),
    };

    /* STEP 3.3 - Validate that both ADC channels are ready */
    if (!adc_is_ready_dt(&adc_channel5))
    {
        LOG_ERR("ADC channel 0 device %s not ready", adc_channel5.dev->name);
        return 0;
    }

    if (!adc_is_ready_dt(&adc_channel4))
    {
        LOG_ERR("ADC channel 1 device %s not ready", adc_channel4.dev->name);
        return 0;
    }

    /* STEP 3.4 - Setup both ADC channels */
    err = adc_channel_setup_dt(&adc_channel5);
    if (err < 0)
    {
        LOG_ERR("Could not setup channel 0 (%d)", err);
        return 0;
    }

    err = adc_channel_setup_dt(&adc_channel4);
    if (err < 0)
    {
        LOG_ERR("Could not setup channel 1 (%d)", err);
        return 0;
    }

    /* STEP 4.2 - Initialize both ADC sequences */
    err = adc_sequence_init_dt(&adc_channel5, &sequence0);
    if (err < 0)
    {
        LOG_ERR("Could not initialize sequence 0");
        return 0;
    }

    err = adc_sequence_init_dt(&adc_channel4, &sequence1);
    if (err < 0)
    {
        LOG_ERR("Could not initialize sequence 1");
        return 0;
    }

    while (1)
    {
        int val_mv0, val_mv1;

        /* STEP 5 - Read samples from both ADC channels */

        // Read channel 0
        err = adc_read(adc_channel5.dev, &sequence0);
        if (err < 0)
        {
            LOG_ERR("Could not read channel 0 (%d)", err);
        }
        else
        {
            val_mv0 = (int)buf0;
            LOG_INF("ADC reading[%u]: %s, channel %d: Raw: %d",
                    count, adc_channel5.dev->name, adc_channel5.channel_id, val_mv0);

            /* STEP 6 - Convert raw value to mV */
            err = adc_raw_to_millivolts_dt(&adc_channel5, &val_mv0);
            if (err < 0)
            {
                LOG_WRN("Channel 0: (value in mV not available)");
            }
            else
            {
                LOG_INF("Channel 0: = %d mV", val_mv0);
            }
        }

        // Read channel 1
        err = adc_read(adc_channel4.dev, &sequence1);
        if (err < 0)
        {
            LOG_ERR("Could not read channel 1 (%d)", err);
        }
        else
        {
            val_mv1 = (int)buf1;
            LOG_INF("ADC reading[%u]: %s, channel %d: Raw: %d",
                    count, adc_channel4.dev->name, adc_channel4.channel_id, val_mv1);

            /* STEP 6 - Convert raw value to mV */
            err = adc_raw_to_millivolts_dt(&adc_channel4, &val_mv1);
            if (err < 0)
            {
                LOG_WRN("Channel 1: (value in mV not available)");
            }
            else
            {
                LOG_INF("Channel 1: = %d mV", val_mv1);
            }
        }

        count++;
        k_sleep(K_MSEC(1000));
    }
    return 0;
}