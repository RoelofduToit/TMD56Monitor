#define _POSIX_C_SOURCE 200809L

#include "measurement.h"

#include "platform/compat.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void measurement_clear(TemperatureMeasurement *m)
{
    m->t1 = nan("");
    m->t2 = nan("");
    m->delta_t = nan("");
    m->elapsed_seconds = 0.0;
    m->valid = false;
    m->suspicious = false;
}

void measurement_set(TemperatureMeasurement *m, double t1, double t2,
                     double elapsed_seconds, bool valid)
{
    m->elapsed_seconds = elapsed_seconds;
    m->suspicious = false;
    m->t1 = isfinite(t1) ? t1 : nan("");
    m->t2 = isfinite(t2) ? t2 : nan("");
    m->valid = valid && isfinite(m->t1) && isfinite(m->t2);
    m->delta_t = m->valid ? (m->t1 - m->t2) : nan("");
    if (m->valid && m->delta_t == 0.0) {
        m->delta_t = 0.0;
    }
}

double measurement_display_tenth(double value)
{
    double snapped;

    if (!isfinite(value)) {
        return nan("");
    }
    snapped = round(value * 10.0) / 10.0;
    if (snapped == 0.0) {
        snapped = 0.0;
    }
    return snapped;
}

bool measurement_format_reading(char *buf, size_t len, double value)
{
    double shown;

    if (buf == NULL || len == 0) {
        return false;
    }
    if (!isfinite(value)) {
        snprintf(buf, len, "    ----");
        return false;
    }
    shown = measurement_display_tenth(value);
    snprintf(buf, len, "%8.1f", shown);
    return true;
}

void measurement_format_triple(char *t1_buf, size_t t1_len, char *t2_buf, size_t t2_len,
                               char *delta_buf, size_t delta_len, double t1, double t2,
                               bool valid)
{
    double shown_t1;
    double shown_t2;

    if (!valid || !isfinite(t1) || !isfinite(t2)) {
        measurement_format_reading(t1_buf, t1_len, nan(""));
        measurement_format_reading(t2_buf, t2_len, nan(""));
        measurement_format_reading(delta_buf, delta_len, nan(""));
        return;
    }
    shown_t1 = measurement_display_tenth(t1);
    shown_t2 = measurement_display_tenth(t2);
    measurement_format_reading(t1_buf, t1_len, shown_t1);
    measurement_format_reading(t2_buf, t2_len, shown_t2);
    measurement_format_reading(delta_buf, delta_len, shown_t1 - shown_t2);
}

void spike_tracker_reset(SpikeTracker *tracker)
{
    tracker->have_baseline = false;
    tracker->baseline_t1 = 0.0;
    tracker->baseline_t2 = 0.0;
    tracker->run_len = 0;
    tracker->run_prev_t1 = 0.0;
    tracker->run_prev_t2 = 0.0;
}

int spike_tracker_observe(SpikeTracker *tracker, TemperatureMeasurement *sample,
                          double threshold_c)
{
    sample->suspicious = false;
    if (!sample->valid) {
        tracker->run_len = 0;
        return 0;
    }

    if (!(threshold_c > 0.0) || !tracker->have_baseline) {
        tracker->have_baseline = true;
        tracker->baseline_t1 = sample->t1;
        tracker->baseline_t2 = sample->t2;
        tracker->run_len = 0;
        return 0;
    }

    if (fabs(sample->t1 - tracker->baseline_t1) <= threshold_c &&
        fabs(sample->t2 - tracker->baseline_t2) <= threshold_c) {
        tracker->baseline_t1 = sample->t1;
        tracker->baseline_t2 = sample->t2;
        tracker->run_len = 0;
        return 0;
    }

    bool continues = false;
    if (tracker->run_len > 0) {
        continues = fabs(sample->t1 - tracker->run_prev_t1) <= threshold_c &&
                    fabs(sample->t2 - tracker->run_prev_t2) <= threshold_c;
    }
    tracker->run_len = continues ? tracker->run_len + 1 : 1;
    tracker->run_prev_t1 = sample->t1;
    tracker->run_prev_t2 = sample->t2;
    sample->suspicious = true;

    if (tracker->run_len >= 3) {
        int marked = tracker->run_len;
        tracker->baseline_t1 = sample->t1;
        tracker->baseline_t2 = sample->t2;
        tracker->run_len = 0;
        sample->suspicious = false;
        return marked;
    }
    return 0;
}

void stats_reset(MeasurementStats *stats)
{
    stats->have_t1 = false;
    stats->have_t2 = false;
    stats->t1_min = 0.0;
    stats->t1_max = 0.0;
    stats->t1_sum = 0.0;
    stats->t2_min = 0.0;
    stats->t2_max = 0.0;
    stats->t2_sum = 0.0;
    stats->t1_count = 0;
    stats->t2_count = 0;
    measurement_clear(&stats->latest);
    stats->have_latest = false;
}

static void accumulate_channel(bool *have, double *min_v, double *max_v,
                               double *sum, unsigned long *count, double value)
{
    if (!*have) {
        *min_v = value;
        *max_v = value;
        *have = true;
    } else {
        if (value < *min_v) {
            *min_v = value;
        }
        if (value > *max_v) {
            *max_v = value;
        }
    }
    *sum += value;
    (*count)++;
}

void stats_accumulate(MeasurementStats *stats, const TemperatureMeasurement *sample,
                      bool ignore_suspicious)
{
    stats->latest = *sample;
    stats->have_latest = true;
    if (!sample->valid) {
        return;
    }
    if (ignore_suspicious && sample->suspicious) {
        return;
    }
    accumulate_channel(&stats->have_t1, &stats->t1_min, &stats->t1_max,
                       &stats->t1_sum, &stats->t1_count, sample->t1);
    accumulate_channel(&stats->have_t2, &stats->t2_min, &stats->t2_max,
                       &stats->t2_sum, &stats->t2_count, sample->t2);
}

#if defined(_WIN32)
double tmd_monotonic_seconds(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;

    if (frequency.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0) {
            return 0.0;
        }
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)frequency.QuadPart;
}

void tmd_format_timestamp(char *buf, size_t len)
{
    FILETIME file_time;
    ULARGE_INTEGER ticks;
    unsigned long long unix_100ns;
    time_t seconds;
    long millis;
    struct tm tm_buf;

    if (buf == NULL || len == 0) {
        return;
    }
    GetSystemTimePreciseAsFileTime(&file_time);
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    if (ticks.QuadPart < 116444736000000000ULL) {
        snprintf(buf, len, "1970-01-01T00:00:00.000");
        return;
    }
    unix_100ns = ticks.QuadPart - 116444736000000000ULL;
    seconds = (time_t)(unix_100ns / 10000000ULL);
    millis = (long)((unix_100ns / 10000ULL) % 1000ULL);
    if (tmd_localtime(&seconds, &tm_buf) != 0) {
        snprintf(buf, len, "1970-01-01T00:00:00.000");
        return;
    }
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03ld",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, millis);
}
#else
double tmd_monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

void tmd_format_timestamp(char *buf, size_t len)
{
    struct timespec ts;
    struct tm tm_buf;

    if (buf == NULL || len == 0) {
        return;
    }
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 ||
        tmd_localtime(&ts.tv_sec, &tm_buf) != 0) {
        snprintf(buf, len, "1970-01-01T00:00:00.000");
        return;
    }

    long millis = ts.tv_nsec / 1000000L;
    if (millis < 0) {
        millis = 0;
    }
    if (millis > 999) {
        millis = 999;
    }
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03ld",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, millis);
}
#endif
