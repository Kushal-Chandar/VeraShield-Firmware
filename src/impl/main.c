#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "led_ctrl.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

void main(void)
{
    int ret = led_ctrl_init();
    if (ret)
    {
        LOG_ERR("led_ctrl_init failed (%d)", ret);
        return;
    }

    /* Enable outputs (OE active-low => drives pin low) */
    led_ctrl_enable(true);
    LOG_INF("LED controller initialized and outputs enabled");

    /* Walk through each defined LED */
    led_id_t leds[] = {LED_RED, LED_GREEN, LED_BLUE, LED_BLT, LED_PW, LED_SPR};
    size_t count = ARRAY_SIZE(leds);

    for (size_t i = 0; i < count; i++)
    {
        LOG_INF("Lighting LED %d", leds[i]);
        led_ctrl_set(leds[i], true);
        k_msleep(1000);
        led_ctrl_set(leds[i], false);
    }

    /* Toggle all LEDs forever */
    while (1)
    {
        LOG_INF("Toggling all LEDs");
        for (size_t i = 0; i < count; i++)
        {
            led_ctrl_toggle(leds[i]);
        }
        k_msleep(1000);
    }
}
