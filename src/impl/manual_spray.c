#include "manual_spray.h"
#include "cycle.h"
#include "slider.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(MANUAL_SPRAY, LOG_LEVEL_INF);

// GPIO definitions
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SP_SW_NODE, gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(SP_LED_NODE, gpios);
static struct gpio_callback button_cb_data;

// Simple timers for each phase
static struct k_timer phase_timer;
static struct k_timer blink_timer;
static struct k_timer monitor_timer;

// State variables
static enum {
    STATE_IDLE,
    STATE_SLOW_BLINK,
    STATE_FAST_BLINK,
    STATE_SOLID,
    STATE_MONITORING_CYCLE
} current_state = STATE_IDLE;

// Function prototypes
static void phase_timer_handler(struct k_timer *timer);
static void blink_timer_handler(struct k_timer *timer);
static void monitor_timer_handler(struct k_timer *timer);
static void start_spray_cycle(void);

static struct k_work start_cycle_work;

static void start_cycle_work_handler(struct k_work *work)
{
    // Move the ADC reading logic here
    int mv = slider_read_millivolts();
    int slider_state = slider_classify_from_mv(mv);

    struct cycle_cfg_t cfg;
    slider_state_to_cycle_cfg(slider_state, &cfg);
    cycle_set_cfg(&cfg);

    LOG_INF("Configured cycle: spray=%dms, idle=%dms, repeats=%d",
            cfg.spray_ms, cfg.idle_ms, cfg.repeats);

    cycle_start();

    // Start monitoring
    k_timer_start(&monitor_timer, K_MSEC(200), K_MSEC(200));
}

void spray_action()
{
    if (current_state != STATE_IDLE)
    {
        LOG_WRN("Sequence already in progress");
        return;
    }

    LOG_INF("Button Pressed - Starting 5s sequence");

    // Start slow blink phase (2 seconds)
    current_state = STATE_SLOW_BLINK;
    gpio_pin_set_dt(&led, 1);

    // Blink every 500ms (slow)
    k_timer_start(&blink_timer, K_MSEC(500), K_MSEC(500));

    // Phase timer: switch to fast blink after 2 seconds
    k_timer_start(&phase_timer, K_MSEC(2000), K_NO_WAIT);
}

void spray_button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // if (current_state != STATE_IDLE)
    // {
    //     LOG_WRN("Sequence already in progress");
    //     return;
    // }

    // LOG_INF("Button Pressed - Starting 5s sequence");

    // // Start slow blink phase (2 seconds)
    // current_state = STATE_SLOW_BLINK;
    // gpio_pin_set_dt(&led, 1);

    // // Blink every 500ms (slow)
    // k_timer_start(&blink_timer, K_MSEC(500), K_MSEC(500));

    // // Phase timer: switch to fast blink after 2 seconds
    // k_timer_start(&phase_timer, K_MSEC(2000), K_NO_WAIT);
    spray_action();
}

static void phase_timer_handler(struct k_timer *timer)
{
    switch (current_state)
    {
    case STATE_SLOW_BLINK:
        LOG_INF("Switching to fast blink");
        current_state = STATE_FAST_BLINK;

        // Change to fast blink (100ms)
        k_timer_stop(&blink_timer);
        k_timer_start(&blink_timer, K_MSEC(100), K_MSEC(100));

        // Phase timer: switch to solid after 2 more seconds (total 4s)
        k_timer_start(&phase_timer, K_MSEC(2000), K_NO_WAIT);
        break;

    case STATE_FAST_BLINK:
        LOG_INF("LED now solid");
        current_state = STATE_SOLID;

        // Stop blinking, LED solid on
        k_timer_stop(&blink_timer);
        gpio_pin_set_dt(&led, 1);

        // Phase timer: start cycle after 1 more second (total 5s)
        k_timer_start(&phase_timer, K_NO_WAIT, K_NO_WAIT);
        break;

    case STATE_SOLID:
        LOG_INF("Sequence complete - starting spray cycle");
        start_spray_cycle();
        break;

    default:
        break;
    }
}

static void blink_timer_handler(struct k_timer *timer)
{
    // Simple toggle for blinking phases
    if (current_state == STATE_SLOW_BLINK || current_state == STATE_FAST_BLINK)
    {
        gpio_pin_toggle_dt(&led);
    }
}

static void monitor_timer_handler(struct k_timer *timer)
{
    if (current_state == STATE_MONITORING_CYCLE)
    {
        struct cycle_state_t cycle_state;
        cycle_get_state(&cycle_state);

        if (cycle_state.phase == 0)
        { // Cycle stopped
            LOG_INF("Spray cycle completed");
            current_state = STATE_IDLE;
            k_timer_stop(&monitor_timer);
            gpio_pin_set_dt(&led, 0);
            // Turn off LED
        }
        else
        {
            // Cycle still active, keep LED on
            gpio_pin_set_dt(&led, 1);
        }
    }
}

static void start_spray_cycle(void)
{
    LOG_INF("Starting spray cycle");

    current_state = STATE_MONITORING_CYCLE;
    gpio_pin_set_dt(&led, 1); // Keep LED solid

    k_work_submit(&start_cycle_work);
}

// Function to check if cycle is active
bool is_spray_cycle_active(void)
{
    if (current_state == STATE_MONITORING_CYCLE)
    {
        struct cycle_state_t cycle_state;
        cycle_get_state(&cycle_state);
        return (cycle_state.phase != 0);
    }
    return false;
}

// Function to manually stop the cycle
void manual_spray_stop(void)
{
    if (current_state == STATE_MONITORING_CYCLE)
    {
        LOG_INF("Manually stopping spray cycle");
        cycle_stop();
        current_state = STATE_IDLE;
        k_timer_stop(&monitor_timer);
        gpio_pin_set_dt(&led, 0);
    }

    // Also stop sequence if in progress
    if (current_state != STATE_IDLE)
    {
        LOG_INF("Stopping sequence");
        current_state = STATE_IDLE;
        k_timer_stop(&phase_timer);
        k_timer_stop(&blink_timer);
        gpio_pin_set_dt(&led, 0);
    }
}

int manual_spray_init(void)
{
    int ret;

    if (!device_is_ready(led.port))
    {
        LOG_ERR("LED GPIO device not ready");
        return -1;
    }

    if (!device_is_ready(button.port))
    {
        LOG_ERR("Button GPIO device not ready");
        return -1;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure LED pin: %d", ret);
        return -1;
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure button pin: %d", ret);
        return -1;
    }

    k_work_init(&start_cycle_work, start_cycle_work_handler);
    // Initialize timers
    k_timer_init(&phase_timer, phase_timer_handler, NULL);
    k_timer_init(&blink_timer, blink_timer_handler, NULL);
    k_timer_init(&monitor_timer, monitor_timer_handler, NULL);

    // Initialize cycle system
    ret = cycle_init();
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize cycle system: %d", ret);
        return -1;
    }

    LOG_INF("Manual spray initialized successfully");
    return 0;
}

int manual_spray_callback(void)
{
    int ret;

    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure button interrupt: %d", ret);
        return ret;
    }

    gpio_init_callback(&button_cb_data, spray_button_pressed, BIT(button.pin));
    ret = gpio_add_callback(button.port, &button_cb_data);
    if (ret < 0)
    {
        LOG_ERR("Failed to add button callback: %d", ret);
        return ret;
    }

    LOG_INF("Button callback configured successfully");
    return 0;
}