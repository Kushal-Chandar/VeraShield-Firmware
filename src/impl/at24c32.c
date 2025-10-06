#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>

#include "at24c32.h"

LOG_MODULE_REGISTER(at24c32, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(i2c0)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

static int at24c32_write_addr(uint16_t addr, const uint8_t *data, size_t len)
{
    uint8_t buf[2 + AT24C32_PAGE_SIZE];

    if (len > AT24C32_PAGE_SIZE)
        return -EINVAL;

    buf[0] = (uint8_t)((addr >> 8) & 0xFF);
    buf[1] = (uint8_t)(addr & 0xFF);

    if (data && len > 0)
    {
        memcpy(&buf[2], data, len);
    }

    return i2c_write(i2c_dev, buf, (uint16_t)(2 + len), AT24C32_ADDR);
}

static int at24c32_read_addr(uint16_t addr, uint8_t *data, size_t len)
{
    uint8_t addr_buf[2];
    addr_buf[0] = (uint8_t)((addr >> 8) & 0xFF);
    addr_buf[1] = (uint8_t)(addr & 0xFF);

    return i2c_write_read(i2c_dev, AT24C32_ADDR, addr_buf, 2, data, len);
}

/* Poll device for end-of-write ACK (acknowledge polling)
 * Use a 1-byte dummy write — portable across controllers.
 */
static int at24c32_wait_ready(void)
{
    /* Typical tWC ~5ms, max 10ms. */
    for (int i = 0; i < 10; i++)
    {
        uint8_t dummy = 0x00;
        /* We only care whether SLA+W is ACKed.
         * Writing a dummy byte is safe: the device will accept it when ready.
         */
        int ret = i2c_write(i2c_dev, &dummy, 1, AT24C32_ADDR);
        if (ret == 0)
            return 0;
        k_msleep(1);
    }

    LOG_ERR("AT24C32 write cycle timeout");
    return -ETIMEDOUT;
}

int at24c32_init(void)
{
    if (!device_is_ready(i2c_dev))
    {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    uint8_t test_data = 0;
    int ret = at24c32_read_addr(0, &test_data, 1);
    if (ret)
    {
        LOG_ERR("AT24C32 probe failed (%d)", ret);
        return ret;
    }

    LOG_INF("AT24C32 EEPROM found at 0x%02X", AT24C32_ADDR);
    return 0;
}

int at24c32_is_ready(void)
{
    return at24c32_wait_ready();
}

int at24c32_write_byte(uint16_t addr, uint8_t data)
{
    if (addr > AT24C32_MAX_ADDR)
        return -EINVAL;

    int ret = at24c32_write_addr(addr, &data, 1);
    if (ret)
    {
        LOG_ERR("Write byte failed at 0x%04X (%d)", addr, ret);
        return ret;
    }

    return at24c32_wait_ready();
}

int at24c32_read_byte(uint16_t addr, uint8_t *data)
{
    if (addr > AT24C32_MAX_ADDR || !data)
        return -EINVAL;

    int ret = at24c32_read_addr(addr, data, 1);
    if (ret)
        LOG_ERR("Read byte failed at 0x%04X (%d)", addr, ret);

    return ret;
}

int at24c32_write_page(uint16_t addr, const uint8_t *data, size_t len)
{
    if (addr > AT24C32_MAX_ADDR || !data || len == 0 || len > AT24C32_PAGE_SIZE)
        return -EINVAL;

    if ((uint32_t)addr + len - 1 > AT24C32_MAX_ADDR)
    {
        LOG_ERR("Write beyond end (addr=0x%04X, len=%zu)", addr, len);
        return -EINVAL;
    }

    if ((addr / AT24C32_PAGE_SIZE) != ((addr + len - 1) / AT24C32_PAGE_SIZE))
    {
        LOG_ERR("Write crosses page boundary (addr=0x%04X, len=%zu)", addr, len);
        return -EINVAL;
    }

    int ret = at24c32_write_addr(addr, data, len);
    if (ret)
    {
        LOG_ERR("Page write failed at 0x%04X (%d)", addr, ret);
        return ret;
    }

    return at24c32_wait_ready();
}

int at24c32_read_bytes(uint16_t addr, uint8_t *data, size_t len)
{
    if (addr > AT24C32_MAX_ADDR || !data || len == 0)
        return -EINVAL;
    if ((uint32_t)addr + len - 1 > AT24C32_MAX_ADDR)
        return -EINVAL;

    int ret = at24c32_read_addr(addr, data, len);
    if (ret)
        LOG_ERR("Read bytes failed at 0x%04X (%d)", addr, ret);

    return ret;
}

int at24c32_write_bytes(uint16_t addr, const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return -EINVAL;
    if (addr > AT24C32_MAX_ADDR)
        return -EINVAL;
    if ((uint32_t)addr + len - 1 > AT24C32_MAX_ADDR)
    {
        LOG_ERR("write_bytes beyond end (addr=0x%04X, len=%zu)", addr, len);
        return -EINVAL;
    }

    uint16_t cur = addr;
    size_t rem = len;

    while (rem > 0)
    {
        size_t page_off = cur % AT24C32_PAGE_SIZE;
        size_t bytes_in_pg = AT24C32_PAGE_SIZE - page_off;
        size_t wlen = (rem < bytes_in_pg) ? rem : bytes_in_pg;

        int rc = at24c32_write_page(cur, data, wlen);
        if (rc)
        {
            LOG_ERR("write_bytes: page write failed at 0x%04X (len=%zu, rc=%d)", cur, wlen, rc);
            return rc;
        }

        cur += (uint16_t)wlen;
        data += wlen;
        rem -= wlen;
    }

    return 0;
}

int at24c32_write_string(uint16_t addr, const char *str)
{
    if (!str)
        return -EINVAL;

    size_t len = strlen(str) + 1; /* include NUL */
    if (addr > AT24C32_MAX_ADDR || (uint32_t)addr + len - 1 > AT24C32_MAX_ADDR)
        return -EINVAL;

    return at24c32_write_bytes(addr, (const uint8_t *)str, len);
}

int at24c32_read_string(uint16_t addr, char *str, size_t max_len)
{
    if (!str || max_len == 0)
        return -EINVAL;
    if (addr > AT24C32_MAX_ADDR)
        return -EINVAL;

    size_t remaining = max_len - 1;
    size_t total = 0;
    uint16_t cur = addr;

    while (remaining > 0 && cur <= AT24C32_MAX_ADDR)
    {
        size_t chunk = remaining;
        if ((uint32_t)cur + chunk - 1 > AT24C32_MAX_ADDR)
        {
            chunk = AT24C32_MAX_ADDR - cur + 1;
            if (chunk == 0)
                break;
        }

        int rc = at24c32_read_bytes(cur, (uint8_t *)str + total, chunk);
        if (rc)
            return rc;

        for (size_t i = 0; i < chunk; i++)
        {
            if (str[total + i] == '\0')
                return 0;
        }

        cur += chunk;
        total += chunk;
        remaining -= chunk;
    }

    str[total] = '\0';
    return 0;
}

int at24c32_clear_page(uint16_t page_num)
{
    if (page_num >= (AT24C32_SIZE / AT24C32_PAGE_SIZE))
        return -EINVAL;

    uint8_t zeros[AT24C32_PAGE_SIZE];
    memset(zeros, 0, sizeof(zeros));

    uint16_t addr = (uint16_t)(page_num * AT24C32_PAGE_SIZE);
    return at24c32_write_page(addr, zeros, sizeof(zeros));
}

int at24c32_update_bits(uint16_t addr, uint8_t mask, uint8_t value)
{
    if (addr > AT24C32_MAX_ADDR)
        return -EINVAL;

    uint8_t b = 0;
    int rc = at24c32_read_byte(addr, &b);
    if (rc)
    {
        LOG_ERR("update_bits: read failed at 0x%04X (%d)", addr, rc);
        return rc;
    }

    uint8_t nb = (uint8_t)((b & ~mask) | (value & mask));
    if (nb == b)
        return 0; /* no change; save a write cycle */

    rc = at24c32_write_byte(addr, nb);

    if (rc)
        LOG_ERR("update_bits: write failed at 0x%04X (%d)", addr, rc);

    return rc;
}
