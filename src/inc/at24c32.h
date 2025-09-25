/*
 * at24c32.h - AT24C32 EEPROM Driver Header
 */

#ifndef AT24C32_H
#define AT24C32_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* AT24C32 I2C Address */
#define AT24C32_ADDR 0x57

/* AT24C32 Specifications */
#define AT24C32_SIZE 4096     /* 4KB total size */
#define AT24C32_PAGE_SIZE 32  /* 32-byte page size */
#define AT24C32_MAX_ADDR 4095 /* Maximum address (0x0FFF) */

/* Write cycle time (typical 5ms, max 10ms) */
#define AT24C32_WRITE_DELAY_MS 10

/* Function prototypes */
int at24c32_init(void);
int at24c32_write_byte(uint16_t addr, uint8_t data);
int at24c32_read_byte(uint16_t addr, uint8_t *data);
int at24c32_write_page(uint16_t addr, const uint8_t *data, size_t len);
int at24c32_read_bytes(uint16_t addr, uint8_t *data, size_t len);
int at24c32_write_string(uint16_t addr, const char *str);
int at24c32_read_string(uint16_t addr, char *str, size_t max_len);
int at24c32_clear_page(uint16_t page_num);
int at24c32_is_ready(void);

#endif /* AT24C32_H */