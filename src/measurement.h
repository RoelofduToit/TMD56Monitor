#ifndef TMD56_MEASUREMENT_H
#define TMD56_MEASUREMENT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * One acquired sample.
 *
 * valid is true only when both channels are finite temperatures.
 * A missing or rejected channel is NAN, never a fabricated 0.
 * suspicious is a display/statistics mark. It does not change t1/t2,
 * and it is not written into the CSV log.
 */
typedef struct {
    double t1;
    double t2;
    double delta_t;
    double elapsed_seconds;
    bool valid;
    bool suspicious;
} TemperatureMeasurement;

typedef struct {
    bool have_t1;
    bool have_t2;
    double t1_min;
    double t1_max;
    double t1_sum;
    double t2_min;
    double t2_max;
    double t2_sum;
    unsigned long t1_count;
    unsigned long t2_count;
    TemperatureMeasurement latest;
    bool have_latest;
} MeasurementStats;

/*
 * Tracks accepted temperatures so a single bad sample can be marked
 * without making the following good sample look like a second spike.
 * Three mutually consistent samples that disagree with the old baseline
 * are treated as a real change and are not left marked.
 */
typedef struct {
    bool have_baseline;
    double baseline_t1;
    double baseline_t2;
    int run_len;
    double run_prev_t1;
    double run_prev_t2;
} SpikeTracker;

#define TMD_DEFAULT_SPIKE_THRESHOLD_C 20.0

void measurement_clear(TemperatureMeasurement *m);
void measurement_set(TemperatureMeasurement *m, double t1, double t2,
                     double elapsed_seconds, bool valid);

/* One decimal place. Non-finite input is rejected. -0.0 becomes 0.0. */
double measurement_display_tenth(double value);

/*
 * Fixed-width reading ("   25.1", "  100.0", "  -10.0") so the decimal
 * column stays put in a monospace or tabular face. Blank is "    ----".
 * Returns false when the value cannot be shown.
 */
bool measurement_format_reading(char *buf, size_t len, double value);

/* T1, T2 and T1-T2 at the displayed precision, so the three figures agree. */
void measurement_format_triple(char *t1_buf, size_t t1_len, char *t2_buf, size_t t2_len,
                               char *delta_buf, size_t delta_len, double t1, double t2,
                               bool valid);

void spike_tracker_reset(SpikeTracker *tracker);

/*
 * Updates sample->suspicious. Temperatures are not modified.
 * If the return value is greater than zero, that many trailing samples
 * (including this one) were a sustained change and should have
 * suspicious cleared.
 */
int spike_tracker_observe(SpikeTracker *tracker, TemperatureMeasurement *sample,
                          double threshold_c);

void stats_reset(MeasurementStats *stats);

/* ignore_suspicious skips min/max/average only. latest is always stored. */
void stats_accumulate(MeasurementStats *stats, const TemperatureMeasurement *sample,
                      bool ignore_suspicious);

double tmd_monotonic_seconds(void);
void tmd_format_timestamp(char *buf, size_t len);

#endif
