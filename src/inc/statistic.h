#ifndef STATISTIC_H
#define STATISTIC_H

#include <stdint.h>
#include <time.h>

struct pcf8563;

#define STAT_ADDR_BASE 0x0FE0u
#define STAT_LEN_BYTES 9u

#define STAT_META_OFS 0u
#define STAT_TIME_OFS 2u

#define STAT_COUNT_MASK 0x3FFFu
#define STAT_STATE_MASK 0x3u
#define STAT_STATE_SHIFT 14

static inline uint16_t stat_pack(uint16_t count, uint8_t state)
{
   return (uint16_t)(((state & STAT_STATE_MASK) << STAT_STATE_SHIFT) |
                     (count & STAT_COUNT_MASK));
}
static inline uint16_t stat_unpack_count(uint16_t v) { return v & STAT_COUNT_MASK; }
static inline uint8_t stat_unpack_state(uint16_t v) { return (v >> STAT_STATE_SHIFT) & STAT_STATE_MASK; }

int statistic_load(uint16_t *count, uint8_t *state, struct tm *t_out);
int statistic_save(uint16_t count, uint8_t state, const struct tm *t);
int statistic_increment_with_rtc(struct pcf8563 *rtc, uint8_t state);

#endif /* STATISTIC_H */
