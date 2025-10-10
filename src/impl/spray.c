#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "spray.h"
#include "cycle.h"
#include "slider.h"
#include "led_ctrl.h"
#include "stats.h"
#include "pcf8563.h"
#include "tm_helpers.h"

LOG_MODULE_REGISTER(SPRAY, LOG_LEVEL_INF);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SP_SW_NODE, gpios);
static struct gpio_callback button_cb_data;

static struct k_timer phase_timer;
static struct k_timer blink_timer;
static struct k_timer monitor_timer;

static enum {
    STATE_IDLE,
    STATE_SLOW_BLINK,
    STATE_FAST_BLINK,
    STATE_SOLID,
    STATE_MONITORING_CYCLE
} current_state = STATE_IDLE;

struct phase_ctx
{
    bool has_state;
    uint8_t state; // only last 2 bits used
};
static struct phase_ctx phase_context;

static void phase_timer_handler(struct k_timer *timer);
static void blink_timer_handler(struct k_timer *timer);
static void monitor_timer_handler(struct k_timer *timer);
static void start_spray_cycle(void);
static void start_spray_cycle_with_state(uint8_t state);

struct cycle_work
{
    struct k_work work;
    bool has_state;
    uint8_t state; // only last 2 bits used
};
static struct cycle_work start_cycle_work;

static void start_cycle_work_handler(struct k_work *work)
{
    struct cycle_work *cw = CONTAINER_OF(work, struct cycle_work, work);

    int mv = slider_read_millivolts();
    int slider_state = slider_classify_from_mv(mv);

    // clamp to 2 bits
    uint8_t chosen_state = cw->has_state ? (cw->state & 0x03) : (uint8_t)(slider_state & 0x03);

    struct cycle_cfg_t cfg_used;
    slider_state_to_cycle_cfg((int)chosen_state, &cfg_used);
    cycle_set_cfg(&cfg_used);

    LOG_INF("Configured cycle: spray=%dms, idle=%dms, repeats=%d (state=%u)",
            cfg_used.spray_ms, cfg_used.idle_ms, cfg_used.repeats, chosen_state);

    {
        struct pcf8563 *rtc = pcf8563_get();
        struct tm now = {0};
        int rc = pcf8563_get_time(rtc, &now);
        if (rc)
        {
            LOG_WRN("RTC read failed: %d (skipping stats append)", rc);
        }
        else if (!tm_sane(&now))
        {
            LOG_WRN("RTC time not sane (skipping stats append)");
        }
        else
        {
            uint8_t inten2b = (uint8_t)(chosen_state & 0x03);
            int ok = stats_append_tm(&now, inten2b);
            if (!ok)
            {
                LOG_WRN("stats: append failed (full or I/O error)");
            }
            else
            {
                uint8_t cnt8 = stats_count();
                if (cnt8 > 0)
                {
                    struct tm ts = {0};
                    uint8_t st = 0xFF;
                    if (stats_get_tm((uint8_t)(cnt8 - 1), &ts, &st))
                    {
                        char buf[64];
                        LOG_INF("stats: count=%u, state=%u, %s",
                                (unsigned)cnt8, (unsigned)st, tm_to_str(&ts, buf, sizeof(buf)));
                    }
                }
            }
        }
    }

    cycle_start();

    k_timer_start(&monitor_timer, K_MSEC(200), K_MSEC(200));
}

void spray_action(void)
{
    if (current_state != STATE_IDLE)
    {
        LOG_WRN("Sequence already in progress");
        return;
    }

    LOG_INF("Button Pressed - Starting sequence (auto)");

    phase_context.has_state = false;

    current_state = STATE_SLOW_BLINK;
    led_spray_set(true);

    k_timer_start(&blink_timer, K_MSEC(500), K_MSEC(500));
    k_timer_start(&phase_timer, K_MSEC(2000), K_NO_WAIT);
}

void ble_spray_caller(uint8_t state)
{
    if (current_state != STATE_IDLE)
    {
        LOG_WRN("Sequence already in progress");
        return;
    }

    state &= 0x03; // only keep last 2 bits

    LOG_INF("BLE spray request (state=%u)", state);

    phase_context.state = state;
    phase_context.has_state = true;

    current_state = STATE_SLOW_BLINK;
    led_spray_set(true);

    k_timer_start(&blink_timer, K_MSEC(500), K_MSEC(500));

    k_timer_start(&phase_timer, K_MSEC(2000), K_NO_WAIT);
}

void spray_button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    spray_action();
}

static void phase_timer_handler(struct k_timer *timer)
{
    struct phase_ctx *ctx = (struct phase_ctx *)k_timer_user_data_get(timer);

    switch (current_state)
    {
    case STATE_SLOW_BLINK:
        LOG_INF("Switching to fast blink");
        current_state = STATE_FAST_BLINK;

        k_timer_stop(&blink_timer);
        k_timer_start(&blink_timer, K_MSEC(100), K_MSEC(100));

        k_timer_start(&phase_timer, K_MSEC(2000), K_NO_WAIT);
        break;

    case STATE_FAST_BLINK:
        LOG_INF("LED now solid");
        current_state = STATE_SOLID;

        k_timer_stop(&blink_timer);
        led_spray_set(true);

        k_timer_start(&phase_timer, K_NO_WAIT, K_NO_WAIT);
        break;

    case STATE_SOLID:
        LOG_INF("Sequence complete - starting spray cycle");
        if (ctx && ctx->has_state)
        {
            start_spray_cycle_with_state(ctx->state & 0x03);
        }
        else
        {
            start_spray_cycle();
        }
        break;

    default:
        break;
    }
}

static void blink_timer_handler(struct k_timer *timer)
{
    if (current_state == STATE_SLOW_BLINK || current_state == STATE_FAST_BLINK)
    {
        led_spray_toggle();
    }
}

static void monitor_timer_handler(struct k_timer *timer)
{
    if (current_state == STATE_MONITORING_CYCLE)
    {
        struct cycle_state_t cycle_state;
        cycle_get_state(&cycle_state);

        if (cycle_state.phase == 0)
        {
            LOG_INF("Spray cycle completed");
            current_state = STATE_IDLE;
            k_timer_stop(&monitor_timer);
            led_spray_set(false);
            phase_context.has_state = false;
        }
        else
        {
            led_spray_set(true);
        }
    }
}

static void start_spray_cycle(void)
{
    LOG_INF("Starting spray cycle (auto/slider)");
    current_state = STATE_MONITORING_CYCLE;
    led_spray_set(true);
    start_cycle_work.has_state = false;
    k_work_submit(&start_cycle_work.work);
}

static void start_spray_cycle_with_state(uint8_t state)
{
    LOG_INF("Starting spray cycle (override state=%u)", state & 0x03);
    current_state = STATE_MONITORING_CYCLE;
    led_spray_set(true);
    start_cycle_work.state = (uint8_t)(state & 0x03);
    start_cycle_work.has_state = true;
    k_work_submit(&start_cycle_work.work);
}

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

void spray_stop(void)
{
    if (current_state == STATE_MONITORING_CYCLE)
    {
        cycle_stop();
        current_state = STATE_IDLE;
        k_timer_stop(&monitor_timer);
        led_spray_set(false);
        phase_context.has_state = false;
    }

    if (current_state != STATE_IDLE)
    {
        LOG_INF("Stopping sequence");
        current_state = STATE_IDLE;
        k_timer_stop(&phase_timer);
        k_timer_stop(&blink_timer);
        led_spray_set(false);
        phase_context.has_state = false;
    }
}

int spray_init(void)
{
    int ret;

    if (!device_is_ready(button.port))
    {
        LOG_ERR("Button GPIO device not ready");
        return -1;
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure button pin: %d", ret);
        return -1;
    }

    k_work_init(&start_cycle_work.work, start_cycle_work_handler);

    k_timer_init(&phase_timer, phase_timer_handler, NULL);
    k_timer_init(&blink_timer, blink_timer_handler, NULL);
    k_timer_init(&monitor_timer, monitor_timer_handler, NULL);

    k_timer_user_data_set(&phase_timer, &phase_context);

    LOG_INF("Manual spray initialized successfully");
    return 0;
}

int spray_callback(void)
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
