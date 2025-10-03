/* main.c — quick test app for AT24C32 driver */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "led_ctrl.h"
#include "at24c32.h"

/* simple assert-like macro that prints and bails on failure */
#define CHECK_OK(expr)                                  \
    do                                                  \
    {                                                   \
        int __rc = (expr);                              \
        if (__rc != 0)                                  \
        {                                               \
            printk("FAIL: %s -> rc=%d\n", #expr, __rc); \
            goto out;                                   \
        }                                               \
        else                                            \
        {                                               \
            printk("OK  : %s\n", #expr);                \
        }                                               \
    } while (0)

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

    /* 3) Page write/read (aligned) */
    {
        const uint16_t page_num = 10;
        const uint16_t page_base = page_num * AT24C32_PAGE_SIZE;
        uint8_t wr[AT24C32_PAGE_SIZE];
        uint8_t rd[AT24C32_PAGE_SIZE];

        for (size_t i = 0; i < sizeof(wr); i++)
        {
            wr[i] = (uint8_t)i; /* simple 0..31 pattern */
        }

        printk("\n[page R/W] page=%u base=0x%04X len=%u\n",
               page_num, page_base, (unsigned)sizeof(wr));
        CHECK_OK(at24c32_write_page(page_base, wr, sizeof(wr)));
        k_msleep(AT24C32_WRITE_DELAY_MS);
        memset(rd, 0, sizeof(rd));
        CHECK_OK(at24c32_read_bytes(page_base, rd, sizeof(rd)));

        if (memcmp(wr, rd, sizeof(wr)) != 0)
        {
            printk("Page data mismatch!\nWR: ");
            dump_hex(wr, sizeof(wr));
            printk("RD: ");
            dump_hex(rd, sizeof(rd));
            goto out;
        }
        else
        {
            printk("[page R/W] verify ✓\n");
        }
    }

    /* 4) String write/read (C-string, includes terminator if driver does that) */
    {
        const uint16_t str_addr = 0x0200;
        const char *msg = "hello from zephyr+AT24C32";
        char buf[64];

        printk("\n[string R/W] addr=0x%04X write=\"%s\"\n", str_addr, msg);
        CHECK_OK(at24c32_write_string(str_addr, msg));
        k_msleep(AT24C32_WRITE_DELAY_MS);
        memset(buf, 0, sizeof(buf));
        CHECK_OK(at24c32_read_string(str_addr, buf, sizeof(buf)));
        printk("[string R/W] read back=\"%s\"\n", buf);

        if (strncmp(msg, buf, strlen(msg)) != 0)
        {
            printk("String mismatch!\n");
            goto out;
        }
        else
        {
            printk("[string R/W] verify ✓\n");
        }
    }

    /* 5) "Clear" page by filling with 0x00 and verifying */
    {
        const uint16_t page_to_clear = 11;
        const uint16_t base = page_to_clear * AT24C32_PAGE_SIZE;
        uint8_t rd[AT24C32_PAGE_SIZE];

        printk("\n[clear page] page=%u (fill with 0x00)\n", page_to_clear);
        CHECK_OK(at24c32_clear_page(page_to_clear)); /* writes 0x00 over the page */
        CHECK_OK(at24c32_is_ready());
        CHECK_OK(at24c32_read_bytes(base, rd, sizeof(rd)));

        bool all_00 = true;
        for (size_t i = 0; i < sizeof(rd); i++)
        {
            if (rd[i] != 0x00)
            {
                all_00 = false;
                break;
            }
        }
        printk("[clear page] verify %s\n", all_00 ? "✓ (all 0x00)" : "✗");
        if (!all_00)
        {
            printk("Page not filled to 0x00\n");
            goto out;
        }
    }

    /* 6) Boundary test: last byte */
    {
        const uint16_t last_addr = AT24C32_MAX_ADDR;
        uint8_t out = 0x3C, in = 0;
        printk("\n[boundary] last byte addr=0x%04X\n", last_addr);
        CHECK_OK(at24c32_write_byte(last_addr, out));
        k_msleep(AT24C32_WRITE_DELAY_MS);
        CHECK_OK(at24c32_read_byte(last_addr, &in));
        printk("[boundary] read back=0x%02X %s\n", in, (in == out) ? "✓" : "✗");
        if (in != out)
        {
            goto out;
        }
    }

    /* 7) Guarded negative test: attempt to read beyond end (expect error) */
    {
        uint8_t rd[8];
        uint16_t near_end = AT24C32_MAX_ADDR - 3; /* 4092 */
        printk("\n[negative] read 8 bytes starting 0x%04X (should fail)\n", near_end);
        int rc = at24c32_read_bytes(near_end, rd, sizeof(rd));
        if (rc == 0)
        {
            printk("Unexpected success reading past end! Driver should bound-check.\n");
            /* not fatal to the rest of tests, just warn */
        }
        else
        {
            printk("Got expected error rc=%d ✓\n", rc);
        }
    }

    printk("\n=== All tests completed ===\n");
out:
    return;
}
