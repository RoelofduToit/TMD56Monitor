#include "history_buffer.h"

#include <stdlib.h>
#include <string.h>

int history_buffer_init_with_limit(HistoryBuffer *history, size_t initial,
                                   size_t max_cap)
{
    memset(history, 0, sizeof(*history));
    if (initial < 2u) {
        initial = 2u;
    }
    if (max_cap < initial) {
        max_cap = initial;
    }
    history->samples = calloc(initial, sizeof(*history->samples));
    if (history->samples == NULL) {
        return -1;
    }
    history->capacity = initial;
    history->max_cap = max_cap;
    return 0;
}

int history_buffer_init(HistoryBuffer *history)
{
    return history_buffer_init_with_limit(history, TMD_HISTORY_DEFAULT_INITIAL,
                                          TMD_HISTORY_DEFAULT_MAX);
}

void history_buffer_free(HistoryBuffer *history)
{
    free(history->samples);
    memset(history, 0, sizeof(*history));
}

void history_buffer_clear(HistoryBuffer *history)
{
    history->count = 0;
    history->head = 0;
}

static int history_grow(HistoryBuffer *history, size_t new_cap)
{
    TemperatureMeasurement *fresh = malloc(new_cap * sizeof(*fresh));
    size_t i;

    if (fresh == NULL) {
        return -1;
    }
    for (i = 0; i < history->count; i++) {
        fresh[i] = history->samples[(history->head + i) % history->capacity];
    }
    free(history->samples);
    history->samples = fresh;
    history->capacity = new_cap;
    history->head = 0;
    return 0;
}

int history_buffer_push(HistoryBuffer *history, const TemperatureMeasurement *sample)
{
    size_t index;

    if (history->samples == NULL || sample == NULL) {
        return -1;
    }
    if (history->count == history->capacity && history->capacity < history->max_cap) {
        size_t grown = history->capacity * 2u;
        if (grown > history->max_cap) {
            grown = history->max_cap;
        }
        if (grown > history->capacity && history_grow(history, grown) != 0) {
            return -1;
        }
    }
    if (history->count == history->capacity) {
        history->head = (history->head + 1u) % history->capacity;
        history->count--;
    }
    index = (history->head + history->count) % history->capacity;
    history->samples[index] = *sample;
    history->count++;
    return 0;
}

size_t history_buffer_count(const HistoryBuffer *history)
{
    return history->count;
}

const TemperatureMeasurement *history_buffer_at(const HistoryBuffer *history,
                                                size_t logical_index)
{
    if (logical_index >= history->count || history->samples == NULL) {
        return NULL;
    }
    return &history->samples[(history->head + logical_index) % history->capacity];
}

TemperatureMeasurement *history_buffer_at_mut(HistoryBuffer *history,
                                              size_t logical_index)
{
    if (logical_index >= history->count || history->samples == NULL) {
        return NULL;
    }
    return &history->samples[(history->head + logical_index) % history->capacity];
}

size_t history_buffer_lower_bound(const HistoryBuffer *history, double elapsed_seconds)
{
    size_t lo = 0;
    size_t hi = history->count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const TemperatureMeasurement *sample = history_buffer_at(history, mid);
        if (sample->elapsed_seconds < elapsed_seconds) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void history_buffer_remark_spikes(HistoryBuffer *history, double threshold_c,
                                  SpikeTracker *end_state)
{
    SpikeTracker tracker;
    size_t i;

    spike_tracker_reset(&tracker);
    for (i = 0; i < history->count; i++) {
        TemperatureMeasurement *sample = history_buffer_at_mut(history, i);
        int unmark = spike_tracker_observe(&tracker, sample, threshold_c);
        if (unmark > 0) {
            size_t nclear = (size_t)unmark;
            size_t from;
            size_t j;
            if (nclear > i + 1u) {
                nclear = i + 1u;
            }
            from = i + 1u - nclear;
            for (j = from; j <= i; j++) {
                history_buffer_at_mut(history, j)->suspicious = false;
            }
        }
    }
    if (end_state != NULL) {
        *end_state = tracker;
    }
}
