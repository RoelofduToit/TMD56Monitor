#ifndef TMD56_HISTORY_BUFFER_H
#define TMD56_HISTORY_BUFFER_H

#include "measurement.h"

#include <stddef.h>

/* Acquisition history. The plot reads this; it does not own a second copy. */
typedef struct {
    TemperatureMeasurement *samples;
    size_t capacity;
    size_t max_cap;
    size_t count;
    size_t head;
} HistoryBuffer;

#define TMD_HISTORY_DEFAULT_INITIAL 4096u
/* 10 Hz for 24 hours. Oldest samples are dropped after this. */
#define TMD_HISTORY_DEFAULT_MAX (24u * 60u * 60u * 10u)

int history_buffer_init(HistoryBuffer *history);
int history_buffer_init_with_limit(HistoryBuffer *history, size_t initial,
                                   size_t max_cap);
void history_buffer_free(HistoryBuffer *history);
void history_buffer_clear(HistoryBuffer *history);

int history_buffer_push(HistoryBuffer *history, const TemperatureMeasurement *sample);
size_t history_buffer_count(const HistoryBuffer *history);

const TemperatureMeasurement *history_buffer_at(const HistoryBuffer *history,
                                                size_t logical_index);
TemperatureMeasurement *history_buffer_at_mut(HistoryBuffer *history,
                                              size_t logical_index);

/* First logical index with elapsed_seconds >= target, or count if none. */
size_t history_buffer_lower_bound(const HistoryBuffer *history, double elapsed_seconds);

/*
 * Recomputes suspicious flags from the raw temperatures.
 * Temperatures themselves are not modified. If end_state is non-NULL it
 * receives the tracker position after the last sample.
 */
void history_buffer_remark_spikes(HistoryBuffer *history, double threshold_c,
                                  SpikeTracker *end_state);

#endif
