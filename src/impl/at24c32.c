#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "at24c32.h"

LOG_MODULE_REGISTER(at24c32, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(i2c0)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

static int at24c32_write_addr(uint16_t addr, const uint8_t *data, size_t len)
{
    uint8_t buf[2 + AT24C32_PAGE_SIZE];

    if (len > AT24C32_PAGE_SIZE)
    {
        return -EINVAL;
    }

    buf[0] = (addr >> 8) & 0xFF;
    buf[1] = addr & 0xFF;

    if (data && len > 0)
    {
        memcpy(&buf[2], data, len);
    }

    return i2c_write(i2c_dev, buf, 2 + len, AT24C32_ADDR);
}

static int at24c32_read_addr(uint16_t addr, uint8_t *data, size_t len)
{
    uint8_t addr_buf[2];
    addr_buf[0] = (addr >> 8) & 0xFF;
    addr_buf[1] = addr & 0xFF;

    return i2c_write_read(i2c_dev, AT24C32_ADDR, addr_buf, 2, data, len);
}

/* Poll device for end-of-write ACK (acknowledge polling) */
static int at24c32_wait_ready(void)
{
    /* Typical tWC ~5ms, max 10ms. Guard for ~100ms to be safe. */
    for (int i = 0; i < 100; i++)
    {
        /* NULL pointer with length 0 is fine; this just probes the slave. */
        int ret = i2c_write(i2c_dev, NULL, 0, AT24C32_ADDR);
        if (ret == 0)
        {
            return 0;
        }
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

    uint8_t test_data;
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
    {
        return -EINVAL;
    }

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
    {
        return -EINVAL;
    }

    int ret = at24c32_read_addr(addr, data, 1);
    if (ret)
    {
        LOG_ERR("Read byte failed at 0x%04X (%d)", addr, ret);
    }
    return ret;
}

int at24c32_write_page(uint16_t addr, const uint8_t *data, size_t len)
{
    if (addr > AT24C32_MAX_ADDR || !data || len == 0 || len > AT24C32_PAGE_SIZE)
    {
        return -EINVAL;
    }

    /* End address must be inside device */
    if ((uint32_t)addr + len - 1 > AT24C32_MAX_ADDR)
    {
        LOG_ERR("Write beyond end (addr=0x%04X, len=%zu)", addr, len);
        return -EINVAL;
    }

    /* Reject page-boundary crossing */
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
    {
        return -EINVAL;
    }
    if ((uint32_t)addr + len - 1 > AT24C32_MAX_ADDR)
    {
        return -EINVAL;
    }

    int ret = at24c32_read_addr(addr, data, len);
    if (ret)
    {
        LOG_ERR("Read bytes failed at 0x%04X (%d)", addr, ret);
    }
    return ret;
}

int at24c32_write_string(uint16_t addr, const char *str)
{
    if (!str)
    {
        return -EINVAL;
    }

    size_t len = strlen(str) + 1; /* include NUL */
    if (addr > AT24C32_MAX_ADDR || (uint32_t)addr + len - 1 > AT24C32_MAX_ADDR)
    {
        return -EINVAL;
    }

    uint16_t current_addr = addr;
    size_t remaining = len;

    while (remaining > 0)
    {
        size_t page_offset = current_addr % AT24C32_PAGE_SIZE;
        size_t bytes_in_page = AT24C32_PAGE_SIZE - page_offset;
        size_t write_len = (remaining < bytes_in_page) ? remaining : bytes_in_page;

        int ret = at24c32_write_page(current_addr,
                                     (const uint8_t *)(str + (len - remaining)), write_len);
        if (ret)
        {
            return ret;
        }

        current_addr += write_len;
        remaining -= write_len;
    }

    return 0;
}

int at24c32_read_string(uint16_t addr, char *str, size_t max_len)
{
    if (!str || max_len == 0)
    {
        return -EINVAL;
    }
    if (addr > AT24C32_MAX_ADDR)
    {
        return -EINVAL;
    }

    /* Read in chunks until NUL or buffer limit. */
    size_t remaining = max_len - 1; /* leave space for terminator */
    size_t total = 0;
    uint16_t cur = addr;

    while (remaining > 0 && cur <= AT24C32_MAX_ADDR)
    {
        size_t chunk = remaining;
        /* Clamp chunk to end of device */
        if ((uint32_t)cur + chunk - 1 > AT24C32_MAX_ADDR)
        {
            chunk = AT24C32_MAX_ADDR - cur + 1;
            if (chunk == 0)
                break;
        }

        int rc = at24c32_read_bytes(cur, (uint8_t *)str + total, chunk);
        if (rc)
        {
            return rc;
        }

        /* Scan for NUL in this chunk */
        for (size_t i = 0; i < chunk; i++)
        {
            if (str[total + i] == '\0')
            {
                return 0; /* done */
            }
        }

        cur += chunk;
        total += chunk;
        remaining -= chunk;
    }

    /* Force-terminate if no NUL encountered in max_len-1 bytes */
    str[total] = '\0';
    return 0;
}

int at24c32_clear_page(uint16_t page_num)
{
    if (page_num >= (AT24C32_SIZE / AT24C32_PAGE_SIZE))
    {
        return -EINVAL;
    }

    /* NOTE: "clear" means program all zeros. 24xx EEPROMs have no erase to 0xFF. */
    uint8_t zeros[AT24C32_PAGE_SIZE];
    memset(zeros, 0, sizeof(zeros));

    uint16_t addr = page_num * AT24C32_PAGE_SIZE;
    return at24c32_write_page(addr, zeros, sizeof(zeros));
}
