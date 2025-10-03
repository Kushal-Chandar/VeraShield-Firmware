#pragma once
#include <zephyr/kernel.h>

int vbat_init(void);
void vbat_start(void);
void vbat_stop(void);
int vbat_last_millivolts(void);
