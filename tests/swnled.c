/*
 * DRY button handling: one generic context, two instances (PW and BLT)
 * Active level is taken from Devicetree (use GPIO_ACTIVE_LOW in DT if needed).
 * Short press: custom per-button action
 * Long press:  custom per-button action (2s default)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h> /* CONTAINER_OF */

/* --- Timings --- */
#define SLEEP_TIME_MS (10 * 60 * 1000) /* 10 minutes */
#define LONG_PRESS_TIME_MS 2000        /* 2 seconds */
#define DEBOUNCE_TIME_MS 50            /* 50 ms */

/* --- Pins from aliases --- */
#define PW_SW_NODE DT_ALIAS(pw_sw)
#define SP_SW_NODE DT_ALIAS(sp_sw)
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec pw_button = GPIO_DT_SPEC_GET(PW_SW_NODE, gpios);
static const struct gpio_dt_spec blt_button = GPIO_DT_SPEC_GET(SP_SW_NODE, gpios);
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

/* --- Per-button state --- */
struct press_state
{
    bool pressed;
    int64_t press_time;
    bool long_handled;
};

/* Per-button actions (short/long) */
typedef void (*btn_action_fn)(void *user);

/* Generic button context */
struct button_ctx
{
    const char *name;
    struct gpio_dt_spec btn;

    /* Press tracking */
    struct press_state st;

    /* Infra */
    struct k_work_delayable work; /* debounced edge handling */
    struct k_timer long_timer;    /* long-press expiry */
    struct gpio_callback irq_cb;

    /* User hooks */
    btn_action_fn on_short;
    btn_action_fn on_long;
    void *user; /* optional payload for actions */
};

/* --- Forward decls --- */
static void btn_irq_common(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void btn_work_handler(struct k_work *work);
static void btn_long_handler(struct k_timer *timer);

/* --- Per-button actions --- */
static void act_pw_short(void *user)
{
    gpio_pin_toggle_dt(&led0);
    printk("PW: short -> toggle LED0\n");
}

static void act_pw_long(void *user)
{
    gpio_pin_set_dt(&led0, 1);
    gpio_pin_set_dt(&led1, 1);
    printk("PW: long -> both LEDs ON\n");
}

static void act_blt_short(void *user)
{
    gpio_pin_toggle_dt(&led1);
    printk("BLT: short -> toggle LED1\n");
}

static void act_blt_long(void *user)
{
    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led1, 0);
    printk("BLT: long -> both LEDs OFF\n");
}

/* --- Context instances --- */
static struct button_ctx pw_ctx = {
    .name = "PW",
    .btn = {0}, /* filled from pw_button in init */
    .on_short = act_pw_short,
    .on_long = act_pw_long,
};

static struct button_ctx blt_ctx = {
    .name = "BLT",
    .btn = {0}, /* filled from blt_button in init */
    .on_short = act_blt_short,
    .on_long = act_blt_long,
};

/* --- Shared handlers --- */
static void btn_irq_common(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(pins);
    struct button_ctx *ctx = CONTAINER_OF(cb, struct button_ctx, irq_cb);
    /* Debounce via delayed work */
    k_work_reschedule(&ctx->work, K_MSEC(DEBOUNCE_TIME_MS));
}

static void btn_work_handler(struct k_work *work)
{
    struct k_work_delayable *dw = CONTAINER_OF(work, struct k_work_delayable, work);
    struct button_ctx *ctx = CONTAINER_OF(dw, struct button_ctx, work);

    int val = gpio_pin_get_dt(&ctx->btn);
    int64_t now = k_uptime_get();

    if (val)
    {
        /* Logical pressed (DT handles ACTIVE_LOW/HIGH) */
        if (!ctx->st.pressed)
        {
            ctx->st.pressed = true;
            ctx->st.press_time = now;
            ctx->st.long_handled = false;
            k_timer_start(&ctx->long_timer, K_MSEC(LONG_PRESS_TIME_MS), K_NO_WAIT);
        }
    }
    else
    {
        /* Released */
        if (ctx->st.pressed)
        {
            ctx->st.pressed = false;
            k_timer_stop(&ctx->long_timer);
            int64_t dur = now - ctx->st.press_time;
            if (!ctx->st.long_handled && dur < LONG_PRESS_TIME_MS)
            {
                if (ctx->on_short)
                    ctx->on_short(ctx->user);
            }
        }
    }
}

static void btn_long_handler(struct k_timer *timer)
{
    struct button_ctx *ctx = k_timer_user_data_get(timer);
    if (ctx->st.pressed && !ctx->st.long_handled)
    {
        if (ctx->on_long)
            ctx->on_long(ctx->user);
        ctx->st.long_handled = true;
    }
}

/* --- Init helpers --- */
static int leds_init(void)
{
    if (!device_is_ready(led0.port) || !device_is_ready(led1.port))
    {
        printk("Error: LED GPIO device not ready\n");
        return -ENODEV;
    }
    int ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (ret)
        return ret;
    ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    return ret;
}

static int button_ctx_init(struct button_ctx *ctx, const struct gpio_dt_spec *src_spec)
{
    ctx->btn = *src_spec;

    if (!device_is_ready(ctx->btn.port))
    {
        printk("Error: %s button device not ready\n", ctx->name);
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&ctx->btn, GPIO_INPUT);
    if (ret)
        return ret;

    /* Interrupt on both edges; add hardware debounce if supported */
#ifdef GPIO_INT_DEBOUNCE
    ret = gpio_pin_interrupt_configure_dt(&ctx->btn, GPIO_INT_EDGE_BOTH | GPIO_INT_DEBOUNCE);
#else
    ret = gpio_pin_interrupt_configure_dt(&ctx->btn, GPIO_INT_EDGE_BOTH);
#endif
    if (ret)
        return ret;

    /* Wire callbacks & timers */
    gpio_init_callback(&ctx->irq_cb, btn_irq_common, BIT(ctx->btn.pin));
    gpio_add_callback(ctx->btn.port, &ctx->irq_cb);

    k_work_init_delayable(&ctx->work, btn_work_handler);

    k_timer_init(&ctx->long_timer, btn_long_handler, NULL);
    k_timer_user_data_set(&ctx->long_timer, ctx);

    printk("%s button ready on %s pin %u\n", ctx->name, ctx->btn.port->name, ctx->btn.pin);
    return 0;
}

/* --- Main --- */
int main(void)
{
    if (leds_init() != 0)
        return -1;

    if (button_ctx_init(&pw_ctx, &pw_button) != 0)
        return -1;
    if (button_ctx_init(&blt_ctx, &blt_button) != 0)
        return -1;

    printk("Buttons: short/long actions armed. Hold >= %d ms for long.\n", LONG_PRESS_TIME_MS);

    while (1)
    {
        k_msleep(SLEEP_TIME_MS);
    }
    return 0;
}
