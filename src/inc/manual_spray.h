#ifndef MANUAL_SPRAY_H
#define MANUAL_SPRAY_H

#include <zephyr/kernel.h>
#include <stdbool.h>

// Device tree node identifiers
#define SP_SW_NODE DT_ALIAS(sp_sw)
#define SP_LED_NODE DT_ALIAS(led1)

// Function declarations
int manual_spray_init(void);
int manual_spray_callback(void);
bool is_spray_cycle_active(void);
void manual_spray_stop(void);
void spray_action();

#endif /* MANUAL_SPRAY_H */