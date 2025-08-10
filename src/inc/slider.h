#pragma once
#include <zephyr/kernel.h>
#include "cycle.h"

int slider_init(void);
int slider_read_millivolts(void);
int slider_classify_from_mv(int mv);
void slider_state_to_cycle_cfg(int state, struct cycle_cfg_t *cfg_out);
