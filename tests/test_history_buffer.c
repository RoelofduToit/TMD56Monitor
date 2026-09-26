#include "history_buffer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect_true(int condition, const char *file, int line, const char *text)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s:%d %s\n", file, line, text);
        failures++;
    }
}

#define EXPECT(condition) expect_true((condition), __FILE__, __LINE__, #condition)

static TemperatureMeasurement sample_at(double elapsed, double t1, double t2, bool valid)
{
    TemperatureMeasurement sample;
    measurement_set(&sample, t1, t2, elapsed, valid);
    return sample;
}

static void test_order_growth_and_overwrite(void)
{
    HistoryBuffer history;
    int i;

    EXPECT(history_buffer_init_with_limit(&history, 4, 8) == 0);
    for (i = 0; i < 10; i++) {
        TemperatureMeasurement sample = sample_at((double)i, (double)i, (double)i + 0.5, true);
        EXPECT(history_buffer_push(&history, &sample) == 0);
    }
    EXPECT(history_buffer_count(&history) == 8u);
    EXPECT(fabs(history_buffer_at(&history, 0)->t1 - 2.0) < 1e-9);
    EXPECT(fabs(history_buffer_at(&history, 7)->t1 - 9.0) < 1e-9);
    EXPECT(fabs(history_buffer_at(&history, 7)->elapsed_seconds - 9.0) < 1e-9);
    EXPECT(history_buffer_lower_bound(&history, 2.0) == 0u);
    EXPECT(history_buffer_lower_bound(&history, 5.5) == 4u);
    EXPECT(history_buffer_lower_bound(&history, 100.0) == 8u);

    history_buffer_clear(&history);
    EXPECT(history_buffer_count(&history) == 0u);
    {
        TemperatureMeasurement sample = sample_at(1.0, 3.0, 4.0, true);
        EXPECT(history_buffer_push(&history, &sample) == 0);
    }
    EXPECT(history_buffer_count(&history) == 1u);
    EXPECT(fabs(history_buffer_at(&history, 0)->t2 - 4.0) < 1e-9);
    history_buffer_free(&history);
}

static void test_spike_marks_do_not_change_temperatures(void)
{
    HistoryBuffer history;
    SpikeTracker tracker;
    TemperatureMeasurement samples[12];
    size_t i;

    samples[0] = sample_at(0, 25.0, 25.0, true);
    samples[1] = sample_at(1, 25.2, 24.9, true);
    samples[2] = sample_at(2, 70.0, 25.0, true);
    samples[3] = sample_at(3, 25.1, 25.0, true);
    samples[4] = sample_at(4, 25.0, -13.0, true);
    samples[5] = sample_at(5, 24.9, 25.1, true);
    samples[6] = sample_at(6, nan(""), nan(""), false);
    samples[7] = sample_at(7, 25.0, 25.0, true);
    samples[8] = sample_at(8, 50.0, 25.0, true);
    samples[9] = sample_at(9, 51.0, 25.5, true);
    samples[10] = sample_at(10, 52.0, 25.2, true);
    samples[11] = sample_at(11, 53.0, 25.4, true);

    EXPECT(history_buffer_init(&history) == 0);
    for (i = 0; i < 12u; i++) {
        EXPECT(history_buffer_push(&history, &samples[i]) == 0);
    }
    history_buffer_remark_spikes(&history, 20.0, &tracker);

    EXPECT(fabs(history_buffer_at(&history, 2)->t1 - 70.0) < 1e-9);
    EXPECT(fabs(history_buffer_at(&history, 4)->t2 - (-13.0)) < 1e-9);
    EXPECT(history_buffer_at(&history, 2)->suspicious);
    EXPECT(!history_buffer_at(&history, 3)->suspicious);
    EXPECT(history_buffer_at(&history, 4)->suspicious);
    EXPECT(!history_buffer_at(&history, 5)->suspicious);
    EXPECT(!history_buffer_at(&history, 6)->suspicious);
    EXPECT(!history_buffer_at(&history, 6)->valid);
    EXPECT(!isfinite(history_buffer_at(&history, 6)->t1));
    EXPECT(!history_buffer_at(&history, 7)->suspicious);
    EXPECT(!history_buffer_at(&history, 8)->suspicious);
    EXPECT(!history_buffer_at(&history, 9)->suspicious);
    EXPECT(!history_buffer_at(&history, 10)->suspicious);
    EXPECT(!history_buffer_at(&history, 11)->suspicious);
    EXPECT(fabs(history_buffer_at(&history, 8)->t1 - 50.0) < 1e-9);
    history_buffer_free(&history);
}

static void test_display_rounding(void)
{
    char t1[16];
    char t2[16];
    char delta[16];

    measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                              9.94, 10.04, true);
    EXPECT(strcmp(t1, "     9.9") == 0);
    EXPECT(strcmp(t2, "    10.0") == 0);
    EXPECT(strcmp(delta, "    -0.1") == 0);

    measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                              99.94, 100.04, true);
    EXPECT(strcmp(t1, "    99.9") == 0);
    EXPECT(strcmp(t2, "   100.0") == 0);
    EXPECT(strcmp(delta, "    -0.1") == 0);

    measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                              25.14, 25.06, true);
    EXPECT(strcmp(t1, "    25.1") == 0);
    EXPECT(strcmp(t2, "    25.1") == 0);
    EXPECT(strcmp(delta, "     0.0") == 0);

    measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                              -10.04, -9.96, true);
    EXPECT(strcmp(t1, "   -10.0") == 0);
    EXPECT(strcmp(t2, "   -10.0") == 0);
    EXPECT(strcmp(delta, "     0.0") == 0);

    measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                              nan(""), 1.0, false);
    EXPECT(strcmp(t1, "    ----") == 0);
    EXPECT(strcmp(delta, "    ----") == 0);
    EXPECT(measurement_display_tenth(-0.0) == 0.0);
    EXPECT(measurement_display_tenth(0.02) == 0.0);

    measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                              INFINITY, -INFINITY, true);
    EXPECT(strcmp(t1, "    ----") == 0);
    EXPECT(strcmp(t2, "    ----") == 0);
    EXPECT(strcmp(delta, "    ----") == 0);
    EXPECT(strstr(t1, "nan") == NULL);
    EXPECT(strstr(t1, "inf") == NULL);

    measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                              -0.0004, 0.0004, true);
    EXPECT(strcmp(t1, "     0.0") == 0);
    EXPECT(strcmp(t2, "     0.0") == 0);
    EXPECT(strcmp(delta, "     0.0") == 0);
    EXPECT(strstr(delta, "-0.0") == NULL);
}

int main(void)
{
    test_order_growth_and_overwrite();
    test_spike_marks_do_not_change_temperatures();
    test_display_rounding();
    if (failures != 0) {
        fprintf(stderr, "%d history buffer check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("history buffer tests passed\n");
    return EXIT_SUCCESS;
}
