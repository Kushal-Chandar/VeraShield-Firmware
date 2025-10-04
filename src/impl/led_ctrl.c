#include "led_ctrl.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_ctrl, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)
#define SPI_NODE DT_NODELABEL(spi1)

/* TLC5916: SPI mode 0, MSB first */
static const struct spi_config spi_cfg = {
    .frequency = 1000000U, /* safe bring-up */
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
};

static const struct device *spi_dev;
static struct gpio_dt_spec le = GPIO_DT_SPEC_GET(USER_NODE, le_gpios);
static struct gpio_dt_spec oe = GPIO_DT_SPEC_GET(USER_NODE, oe_gpios);

static uint8_t shadow_byte; /* last latched value */

/* Internal: shift & latch one byte while outputs are blanked */
static int tlc5916_latch_byte(uint8_t val)
{
    struct spi_buf txb = {.buf = &val, .len = 1};
    struct spi_buf_set tx = {.buffers = &txb, .count = 1};

    /* Blank outputs while shifting (OE active-low) */
    gpio_pin_set_dt(&oe, 1);

    int ret = spi_write(spi_dev, &spi_cfg, &tx);
    if (ret < 0)
    {
        LOG_ERR("spi_write failed: %d", ret);
        return ret;
    }

    /* LE rising edge to latch */
    k_busy_wait(5);
    gpio_pin_set_dt(&le, 1);
    k_busy_wait(5);
    gpio_pin_set_dt(&le, 0);

    return 0;
}

/* Public API --------------------------------------------------------------- */

int led_ctrl_init(void)
{
    int ret;

    spi_dev = DEVICE_DT_GET(SPI_NODE);
    if (!device_is_ready(spi_dev))
    {
        LOG_ERR("SPI not ready");
        return -ENODEV;
    }

    if (!device_is_ready(le.port) || !device_is_ready(oe.port))
    {
        LOG_ERR("GPIO ports not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&le, GPIO_OUTPUT_INACTIVE);
    if (ret)
    {
        LOG_ERR("LE cfg failed: %d", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&oe, GPIO_OUTPUT_INACTIVE);
    if (ret)
    {
        LOG_ERR("OE cfg failed: %d", ret);
        return ret;
    }

    /* Start with everything off in hardware and shadow */
    shadow_byte = 0x00;
    ret = tlc5916_latch_byte(shadow_byte);
    if (ret)
        return ret;

    return 0;
}

void led_ctrl_enable(bool enable)
{
    gpio_pin_set_dt(&oe, enable ? 1 : 0);
}

int led_ctrl_write(uint8_t value)
{
    int ret = tlc5916_latch_byte(value);
    if (ret == 0)
    {
        shadow_byte = value;
    }
    return ret;
}

uint8_t led_ctrl_read_shadow(void)
{
    return shadow_byte;
}

static inline uint8_t bit_mask_from_id(led_id_t id)
{
    return (uint8_t)(1u << (uint8_t)id); /* OUT# == bit# */
}

int led_ctrl_set(led_id_t id, bool on)
{
    if ((int)id < 0 || (int)id > 7)
        return -EINVAL;

    uint8_t m = bit_mask_from_id(id);
    uint8_t v = on ? (shadow_byte | m) : (shadow_byte & (uint8_t)~m);
    return led_ctrl_write(v);
}

int led_ctrl_toggle(led_id_t id)
{
    if ((int)id < 0 || (int)id > 7)
        return -EINVAL;

    uint8_t m = bit_mask_from_id(id);
    return led_ctrl_write(shadow_byte ^ m);
}

int led_ctrl_all_on(void)
{
    return led_ctrl_write(0xFF);
}

int led_ctrl_all_off(void)
{
    return led_ctrl_write(0x00);
}
