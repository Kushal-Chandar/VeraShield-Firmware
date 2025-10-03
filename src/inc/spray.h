#ifndef SPRAY_H
#define SPRAY_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include "cycle.h"

#define SP_SW_NODE DT_ALIAS(sp_sw)
#define SP_LED_NODE DT_ALIAS(led1)

int spray_init(void);
int spray_callback(void);
bool is_spray_cycle_active(void);
void spray_stop(void);
void spray_action_with_cfg(struct cycle_cfg_t);

#endif /* SPRAY_H */