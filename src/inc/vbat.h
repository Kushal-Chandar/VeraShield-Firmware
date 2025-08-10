#pragma once
#include <zephyr/kernel.h>

int vbat_init(void);   /* setup ADC + LED3 */
void vbat_start(void); /* start periodic sampling + blinking */
void vbat_stop(void);
int vbat_last_millivolts(void);
