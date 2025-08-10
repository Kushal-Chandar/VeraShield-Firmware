#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "cycle.h"
#include "servo.h" /* if you use it */
#include "vbat.h"
#include "slider.h"

LOG_MODULE_REGISTER(APP, LOG_LEVEL_INF);

/* Use board button alias sw0 (change if needed) */
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* Simple debounce via delayed work */
static struct k_work_delayable btn_work;

static void handle_button(struct k_work *work)
{
    ARG_UNUSED(work);

    /* 1) Read slider now */
    int mv = slider_read_millivolts();
    if (mv < 0)
    {
        LOG_ERR("slider read failed: %d", mv);
        return;
    }

    /* 2) Map to state and then to cycle cfg */
    int st = slider_classify_from_mv(mv);

    struct cycle_cfg_t cfg;
    slider_state_to_cycle_cfg(st, &cfg);

    /* 3) Apply and start the cycle */
    (void)cycle_set_cfg(&cfg);
    cycle_start();

    LOG_INF("Button: mv=%d -> state=%d -> cycle: spray=%u idle=%u reps=%u",
            mv, st, cfg.spray_ms, cfg.idle_ms, cfg.repeats);
}

static void btn_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    /* Debounce / rate-limit actual work by scheduling a short delay */
    k_work_schedule(&btn_work, K_MSEC(20));
}

static struct gpio_callback btn_cb_data;

int main(void)
{
    LOG_INF("App start");

    /* Init cycle engine + tick */
    cycle_init();
    cycle_tick_start();

    /* Optional initial config (will be replaced on first button press) */
    struct cycle_cfg_t init = {.spray_ms = 2000, .idle_ms = 3000, .repeats = 0};
    cycle_set_cfg(&init);

    /* VBAT LED3 blinker */
    if (vbat_init() == 0)
        vbat_start();

    /* Slider ADC (no timers) */
    if (slider_init() != 0)
    {
        LOG_ERR("slider_init failed");
    }

    /* Button setup */
    if (!device_is_ready(btn.port))
    {
        LOG_ERR("Button GPIO not ready");
    }
    else
    {
        gpio_pin_configure_dt(&btn, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
        gpio_init_callback(&btn_cb_data, btn_cb, BIT(btn.pin));
        gpio_add_callback(btn.port, &btn_cb_data);
        k_work_init_delayable(&btn_work, handle_button);
        LOG_INF("Button ready on %s pin %u", btn.port->name, btn.pin);
    }

    /* Idle loop: log state occasionally */
    while (1)
    {
        struct cycle_state_t st;
        cycle_get_state(&st);
        LOG_DBG("cycle: phase=%u rem=%u idx=%u | vbat=%d mV",
                st.phase, st.remaining_ms, st.cycle_index, vbat_last_millivolts());
        k_msleep(1000);
    }
    return 0;
}
