#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/*
 * ===== Statistics storage layout (AT24C32) =====
 *
 * STATS_BASE (32B-aligned):
 *   [count : u8]                               // 0..STATS_CAP (<=254)
 *   [times : STATS_CAP × 7 bytes]              // contiguous
 *   [intensities : ceil(STATS_CAP/4) bytes]    // 2 bits per entry
 *
 * Entry i:
 *   time      @ (STATS_TIMES_OFF + 7*i)
 *   intensity = bits [2*(i%4) .. 2*(i%4)+1] of byte @ (STATS_INT_OFF + i/4)
 */

// ===================== CONFIG =====================

// Pick a 32-byte aligned base inside the 4KB space. Example 0x0600:
#define STATS_BASE 0x0600u
// No magic byte; keep 0xFF as "invalid/uninitialized". Cap at 254.
#define STATS_CAP 254u

#define TIME_LEN 7u

// ================= Derived layout =================
#define STATS_COUNT_OFF (STATS_BASE + 0u)
#define STATS_TIMES_OFF (STATS_BASE + 1u)
#define STATS_TIMES_LEN ((uint32_t)STATS_CAP * TIME_LEN)
#define STATS_INT_OFF (STATS_TIMES_OFF + STATS_TIMES_LEN)
#define STATS_INT_LEN ((STATS_CAP + 3u) / 4u) // ceil(N/4)
#define STATS_TOTAL_LEN (1u + STATS_TIMES_LEN + STATS_INT_LEN)

#ifdef __cplusplus
extern "C"
{
#endif

   void stats_init_if_blank(void);
   int stats_append(const uint8_t time[TIME_LEN], uint8_t intensity2b);
   int stats_get(uint8_t index, uint8_t out_time[TIME_LEN], uint8_t *out_int2b);
   uint8_t stats_count(void);
   void stats_clear(void);

   int stats_append_tm(const struct tm *t, uint8_t intensity2b);
   int stats_get_tm(uint8_t index, struct tm *out_t, uint8_t *out_int2b);

#ifdef __cplusplus
}
#endif
