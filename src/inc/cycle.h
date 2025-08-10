#pragma once
#include <stdint.h>

/*
 * Cycle configuration: fixed servo angles.
 * Only durations and repeat count are configurable.
 */
struct __packed cycle_cfg_t
{
    uint16_t spray_ms; /* spray duration in ms */
    uint16_t idle_ms;  /* idle duration in ms */
    uint16_t repeats;  /* number of Spray->Idle cycles; 0 = infinite */
};

/* Runtime state */
struct __packed cycle_state_t
{
    uint8_t phase;         /* 0 Stopped, 1 Spray, 2 Idle, 3 Paused */
    uint16_t remaining_ms; /* ms left in current phase */
    uint16_t cycle_index;  /* completed Spray->Idle iterations */
};

int cycle_init(void);
void cycle_tick_start(void);
void cycle_tick_stop(void);

int cycle_set_cfg(const struct cycle_cfg_t *cfg);
void cycle_get_cfg(struct cycle_cfg_t *cfg_out);
void cycle_get_state(struct cycle_state_t *st_out);

void cycle_start(void);
void cycle_stop(void);
void cycle_pause(void);
void cycle_resume(void);
