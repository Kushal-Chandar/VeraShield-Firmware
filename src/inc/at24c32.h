#ifndef AT24C32_H
#define AT24C32_H

#include <zephyr/devicetree.h>
#include <stdint.h>
#include <stddef.h>

/* Devicetree node for the EEPROM: label "at24c32" in your overlay */
#define AT24C32_NODE DT_NODELABEL(at24c32)

#if !DT_NODE_HAS_STATUS(AT24C32_NODE, okay)
#error "AT24C32 devicetree node 'at24c32' not found or not okay"
#endif

/* I2C address from devicetree reg = <0x50>; */
#define AT24C32_ADDR DT_REG_ADDR(AT24C32_NODE)

/* 32 Kbit (4 KB) device */
#define AT24C32_SIZE 4096                    /* 4KB total size */
#define AT24C32_PAGE_SIZE 32u                /* 32-byte page size */
#define AT24C32_MAX_ADDR (AT24C32_SIZE - 1u) /* Maximum address (0x0FFF) */

int at24c32_init(void);
int at24c32_is_ready(void);

int at24c32_write_byte(uint16_t addr, uint8_t data);
int at24c32_read_byte(uint16_t addr, uint8_t *data);

int at24c32_write_page(uint16_t addr, const uint8_t *data, size_t len);

int at24c32_read_bytes(uint16_t addr, uint8_t *data, size_t len);
int at24c32_write_bytes(uint16_t addr, const uint8_t *data, size_t len);

int at24c32_write_string(uint16_t addr, const char *str);
int at24c32_read_string(uint16_t addr, char *str, size_t max_len);

int at24c32_clear_page(uint16_t page_num);

int at24c32_update_bits(uint16_t addr, uint8_t mask, uint8_t value);

#endif /* AT24C32_H */
