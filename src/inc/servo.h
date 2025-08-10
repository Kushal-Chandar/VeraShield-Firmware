#pragma once
#include <stdint.h>

int servo_init(void);
void servo_set_deg(uint16_t deg); /* 0..180 */
int servo_disable(void);
uint16_t servo_get_deg(void);
