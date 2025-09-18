#include "cycle.h"
#include "servo.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(CYCLES, LOG_LEVEL_INF);

/* Fixed angles (degrees) */
#define SPRAY_DEG 20
#define IDLE_DEG 110

/* Defaults: 5 s spray, 7 s idle, 1 time */
static struct cycle_cfg_t s_cfg = {5000, 2000, 1};
static struct cycle_state_t s_state = {0, 0, 0};

static bool s_running = false;
static bool s_paused = false;
static int32_t s_phase_end_ms = 0;

static inline int32_t now_ms(void) { return (int32_t)k_uptime_get_32(); }

static void enter_spray(void)
{
    s_state.phase = 1;
    servo_set_deg(SPRAY_DEG);
    s_phase_end_ms = now_ms() + s_cfg.spray_ms;
    s_state.remaining_ms = s_cfg.spray_ms;
    LOG_INF("SPRAY for %u ms", s_cfg.spray_ms);
}

static void enter_idle(void)
{
    s_state.phase = 2;
    servo_set_deg(IDLE_DEG);
    s_phase_end_ms = now_ms() + s_cfg.idle_ms;
    s_state.remaining_ms = s_cfg.idle_ms;
    LOG_INF("IDLE for %u ms", s_cfg.idle_ms);
}

static void tick_work(struct k_work *w);
K_WORK_DELAYABLE_DEFINE(cycle_work, tick_work);

static void tick_work(struct k_work *w)
{
    if (s_running && !s_paused)
    {
        int32_t rem = s_phase_end_ms - now_ms();
        s_state.remaining_ms = (rem > 0) ? (uint16_t)rem : 0;

        if (rem <= 0)
        {
            if (s_state.phase == 1)
            {
                enter_idle();
            }
            else if (s_state.phase == 2)
            {
                if (s_cfg.repeats && (++s_state.cycle_index >= s_cfg.repeats))
                {
                    s_running = false;
                    s_state.phase = 0;
                    s_state.remaining_ms = 0;
                    servo_set_deg(IDLE_DEG);
                    LOG_INF("DONE. Ran %u cycles.", s_state.cycle_index);
                }
                else
                {
                    enter_spray();
                }
            }
        }
    }

    k_work_schedule(&cycle_work, K_MSEC(100));
}

/* API */
int cycle_init(void)
{
    servo_set_deg(IDLE_DEG);
    return 0;
}

void cycle_tick_start(void) { k_work_schedule(&cycle_work, K_MSEC(100)); }
void cycle_tick_stop(void) { k_work_cancel_delayable(&cycle_work); }

int cycle_set_cfg(const struct cycle_cfg_t *cfg)
{
    if (!cfg)
    {
        return -EINVAL;
    }
    s_cfg = *cfg; /* angles are fixed; only times/repeats copied */
    return 0;
}

void cycle_get_cfg(struct cycle_cfg_t *o) { *o = s_cfg; }
void cycle_get_state(struct cycle_state_t *o) { *o = s_state; }

void cycle_start(void)
{
    s_running = true;
    s_paused = false;
    s_state.cycle_index = 0;
    enter_spray();
}

void cycle_stop(void)
{
    s_running = false;
    s_paused = false;
    s_state.phase = 0;
    s_state.remaining_ms = 0;
    servo_set_deg(IDLE_DEG);
    LOG_INF("STOP");
}

void cycle_pause(void)
{
    if (s_running)
    {
        s_paused = true;
        s_state.phase = 3;
        LOG_INF("PAUSE");
    }
}

void cycle_resume(void)
{
    if (s_running)
    {
        s_paused = false;
        LOG_INF("RESUME");
    }
}
