#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ERA_MACRO_QUEUE_CAPACITY 8U

/* Requests only enqueue; the task starts at most one snapshot per call.
 * One active snapshot, then eight FIFO IDs resolved when they start. A full
 * FIFO rejects the newest request and keeps the already accepted prefix. */
bool era_macro_request(uint8_t id);
void era_macro_task(void);
void era_macro_session(uint32_t generation, bool valid, bool suspended);
void era_macro_cancel(void);
bool era_macro_report_generation(uint32_t *generation);

typedef struct {
    uint32_t accepted;
    uint32_t rejected_full;
    uint32_t rejected_invalid;
    uint32_t malformed;
    uint32_t canceled;
    bool active;
    uint8_t queued;
} era_macro_stats_t;
void era_macro_get_stats(era_macro_stats_t *stats);
