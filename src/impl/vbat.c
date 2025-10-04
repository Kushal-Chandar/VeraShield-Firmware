#include "vbat.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/services/bas.h>

LOG_MODULE_REGISTER(VBAT, LOG_LEVEL_INF);

/* ADC: /zephyr,user named "vbat" */
static const struct adc_dt_spec adc_vbat =
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), vbat);

/* LED: alias led3 */
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static struct k_work_delayable blink_work;
static struct k_work_delayable sample_work;

static volatile int last_mv = -1;
static volatile uint8_t battery_percent = 0;
static uint32_t blink_period_ms = 1000;
static bool running = false;
static bool led_on = false;

#define VBAT_FULL_MV 3300   // 100% battery level
#define VBAT_EMPTY_MV 2000  // 0% battery level
#define VBAT_GREEN_MV 2600  // Good battery level
#define VBAT_YELLOW_MV 2000 // Medium battery level
#define VBAT_RED_MV 1500    // Low battery level

#define ADC_SAMPLE_INTERVAL_MS (5 * 60 * 1000)
// #define ADC_SAMPLE_INTERVAL_MS (5 * 1000)

static uint8_t voltage_to_percent(int mv)
{
    if (mv <= VBAT_EMPTY_MV)
    {
        return 0;
    }
    if (mv >= VBAT_FULL_MV)
    {
        return 100;
    }

    // Linear interpolation between empty and full
    int range = VBAT_FULL_MV - VBAT_EMPTY_MV;
    int value = mv - VBAT_EMPTY_MV;
    return (uint8_t)((value * 100) / range);
}

/**
 * Determine LED blink period based on battery voltage
 * Lower voltage = faster blinking = more urgent warning
 */
static uint32_t decide_period_ms(int mv)
{
    if (mv < 0)
        return 1000; // No reading yet - slow blink
    if (mv >= VBAT_GREEN_MV)
        return 1000; // 1 Hz - good battery (slow blink)
    if (mv >= VBAT_YELLOW_MV)
        return 500; // 2 Hz - medium battery
    if (mv >= VBAT_RED_MV)
        return 250; // 4 Hz - low battery (fast blink)
    return 125;     // 8 Hz - critical battery (very fast blink)
}

/**
 * LED blinking function - toggles LED on/off
 * This creates the blinking effect by scheduling itself repeatedly
 */
static void blink_fn(struct k_work *work)
{
    ARG_UNUSED(work); // We don't use the work parameter

    if (!running)
    {
        return; // Stop blinking if system is not running
    }

    // Toggle LED state
    led_on = !led_on;
    gpio_pin_set_dt(&led3, led_on);

    // Schedule next toggle in half the period
    // (so full on-off cycle takes the complete period)
    k_work_schedule(&blink_work, K_MSEC(blink_period_ms / 2U));
}

/**
 * ADC sampling function - runs every 5 minutes
 * This function:
 * 1. Reads battery voltage via ADC
 * 2. Updates blink rate based on voltage
 * 3. Converts voltage to percentage for BAS
 * 4. Updates Bluetooth Battery Service
 * 5. Schedules next reading in 5 minutes
 */
static void sample_fn(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!running)
    {
        LOG_INF("ADC sampling stopped - system not running");
        return;
    }

    int err;
    int16_t raw; // Raw ADC reading (16-bit signed integer)

    // Setup ADC sequence - tells ADC driver where to store result
    struct adc_sequence seq = {
        .buffer = &raw,             // Pointer to storage location
        .buffer_size = sizeof(raw), // Size of storage (2 bytes)
    };

    LOG_INF("Reading battery voltage...");

    // Step 1: Initialize ADC sequence from device tree configuration
    // This sets up timing, gain, reference voltage, etc. from your .dts file
    err = adc_sequence_init_dt(&adc_vbat, &seq);
    if (!err)
    {
        // Step 2: Perform actual ADC reading
        err = adc_read(adc_vbat.dev, &seq);
    }

    if (!err)
    {
        // Step 3: Convert raw ADC value to millivolts
        int mv = (int)raw; // Start with raw value

        // This function uses ADC configuration (gain, vref) to convert to mV
        if (adc_raw_to_millivolts_dt(&adc_vbat, &mv) < 0)
        {
            LOG_WRN("Millivolt conversion not available - using raw value");
            // If conversion fails, mv contains raw value
        }

        // Step 4: Store results and update blink rate
        last_mv = mv;
        blink_period_ms = decide_period_ms(mv);
        battery_percent = voltage_to_percent(mv);

        LOG_INF("Battery: raw=%d mv=%d percent=%d%% -> blink %ums",
                raw, mv, battery_percent, blink_period_ms);

        // Step 5: Update Bluetooth Battery Service
        // This sends battery percentage to connected Bluetooth devices
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

    // Step 6: Schedule next ADC reading in 5 minutes
    k_work_schedule(&sample_work, K_MSEC(ADC_SAMPLE_INTERVAL_MS));
    LOG_INF("Next battery reading scheduled in 5 minutes");
}

/**
 * Initialize battery monitoring system
 * Sets up ADC, LED, and work queues
 */
int vbat_init(void)
{
    int err;

    LOG_INF("Initializing battery monitoring...");

    // Step 1: Check if ADC device is ready
    if (!adc_is_ready_dt(&adc_vbat))
    {
        LOG_ERR("ADC %s not ready", adc_vbat.dev->name);
        return -ENODEV;
    }

    // Step 2: Configure ADC channel using device tree settings
    err = adc_channel_setup_dt(&adc_vbat);
    if (err)
    {
        LOG_ERR("adc_channel_setup_dt failed: %d", err);
        return err;
    }
    LOG_INF("ADC channel configured");

    // Step 3: Check and configure LED GPIO
    if (!device_is_ready(led3.port))
    {
        LOG_ERR("LED3 GPIO not ready");
        return -ENODEV;
    }

    // Configure LED as output, initially off
    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE);
    LOG_INF("LED3 configured");

    // Step 4: Initialize work queues
    // Link our functions to work queue items
    k_work_init_delayable(&blink_work, blink_fn);   // For LED blinking
    k_work_init_delayable(&sample_work, sample_fn); // For ADC sampling

    LOG_INF("Battery monitoring initialization complete");
    return 0;
}

/**
 * Start battery monitoring system
 * Begins LED blinking and periodic ADC sampling
 */
void vbat_start(void)
{
    if (running)
    {
        LOG_INF("Battery monitoring already running");
        return;
    }

    LOG_INF("Starting battery monitoring...");
    running = true;
    led_on = false;
    gpio_pin_set_dt(&led3, 0); // Start with LED off

    // Start ADC sampling immediately, then every 5 minutes
    k_work_schedule(&sample_work, K_NO_WAIT);

    // Start LED blinking with current blink rate
    k_work_schedule(&blink_work, K_MSEC(blink_period_ms / 2U));

    LOG_INF("Battery monitoring started - first reading immediate, then every 5 minutes");
}

/**
 * Stop battery monitoring system
 * Cancels all scheduled work and turns off LED
 */
void vbat_stop(void)
{
    if (!running)
    {
        LOG_INF("Battery monitoring already stopped");
        return;
    }

    LOG_INF("Stopping battery monitoring...");
    running = false;

    // Cancel all pending work
    k_work_cancel_delayable(&sample_work);
    k_work_cancel_delayable(&blink_work);

    // Turn off LED
    gpio_pin_set_dt(&led3, 0);

    LOG_INF("Battery monitoring stopped");
}

/**
 * Get last measured voltage in millivolts
 * Other parts of your code can call this
 */
int vbat_last_millivolts(void)
{
    return last_mv;
}

/**
 * Get last calculated battery percentage
 * Useful for displaying battery level elsewhere in your app
 */
uint8_t vbat_last_percent(void)
{
    return battery_percent;
}

/**
 * Force immediate ADC reading (for testing)
 * This doesn't disrupt the 5-minute schedule
 */
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