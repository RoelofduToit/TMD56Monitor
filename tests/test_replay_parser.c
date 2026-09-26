#define _POSIX_C_SOURCE 200809L

#include "platform/compat.h"
#include "sources/replay_source.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

static int failures = 0;

static void expect_true(int condition, const char *file, int line, const char *text)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s:%d %s\n", file, line, text);
        failures++;
    }
}

#define EXPECT(condition) expect_true((condition), __FILE__, __LINE__, #condition)

static int write_temp(char *path, size_t path_len, const char *contents)
{
    char dir[512];
    FILE *file;
    static unsigned seq = 0;
    size_t n;
    int written;

    if (tmd_temp_dir(dir, sizeof dir, "tmd56-replay") != 0) {
        return -1;
    }
    n = strlen(dir);
    written = snprintf(path, path_len, "%s%sreplay-%u.txt", dir,
                       (n > 0u && (dir[n - 1u] == '/' || dir[n - 1u] == '\\')) ? "" : "/",
                       seq++);
    if (written < 0 || (size_t)written >= path_len) {
        return -1;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fputs(contents, file) < 0) {
        fclose(file);
        remove(path);
        return -1;
    }
    if (fclose(file) != 0) {
        remove(path);
        return -1;
    }
    return 0;
}

static void test_tmd_export(void)
{
    static const char fixture[] =
        "File Name:\tdemo.txt\n"
        "User Name:\t\n"
        "Description:\n"
        "\n"
        "Start Time:01/13/2023   16:04:39\n"
        "\n"
        "\tID\tComport\t\tChannel\tType\t\tUnit\n"
        "\t1\tCOM3\t\tCH1\tK\t\tDeg-C\t\n"
        "\t1\tCOM3\t\tCH2\tK\t\tDeg-C\t\n"
        "\n"
        "\n"
        "No.\tDate\t\tTime\t\tCH01\tCH02\tCH03\n"
        "52\t2023/01/13\t16:04:39\t24.4\t24.8\t\n"
        "53\t2023/01/13\t16:04:40\t24.3\t24.7\t\n"
        "oven door opened\n"
        "54\t2023/01/13\t16:04:41\tOL\t24.6\t\n"
        "55\t2023/01/13\t16:04:42\t24.0\t24.5\t\n";
    char path[512];
    char error[128];
    ReplaySample *samples = NULL;
    size_t count = 0;

    EXPECT(write_temp(path, sizeof path, fixture) == 0);
    EXPECT(replay_parse_path(path, &samples, &count, NULL, error, sizeof error) == 0);
    EXPECT(count == 4u);
    if (count == 4u && samples != NULL) {
        EXPECT(fabs(samples[0].elapsed_seconds - 0.0) < 1e-9);
        EXPECT(fabs(samples[0].t1 - 24.4) < 1e-9);
        EXPECT(fabs(samples[0].t2 - 24.8) < 1e-9);
        EXPECT(samples[0].valid);
        EXPECT(fabs(samples[1].elapsed_seconds - 1.0) < 1e-9);
        EXPECT(fabs(samples[2].elapsed_seconds - 2.0) < 1e-9);
        EXPECT(!samples[2].valid);
        EXPECT(!isfinite(samples[2].t1));
        EXPECT(samples[2].t1 != 0.0);
        EXPECT(fabs(samples[2].t2 - 24.6) < 1e-9);
        EXPECT(fabs(samples[3].elapsed_seconds - 3.0) < 1e-9);
        EXPECT(samples[3].valid);
    }
    replay_samples_free(samples);
    remove(path);
}

static void test_midnight_wrap(void)
{
    static const char fixture[] =
        "1\t2023/01/13\t23:59:58\t1.0\t2.0\n"
        "2\t2023/01/13\t23:59:59\t1.1\t2.1\n"
        "3\t2023/01/14\t00:00:01\t1.2\t2.2\n";
    char path[512];
    char error[128];
    ReplaySample *samples = NULL;
    size_t count = 0;

    EXPECT(write_temp(path, sizeof path, fixture) == 0);
    EXPECT(replay_parse_path(path, &samples, &count, NULL, error, sizeof error) == 0);
    EXPECT(count == 3u);
    if (count == 3u && samples != NULL) {
        EXPECT(fabs(samples[0].elapsed_seconds - 0.0) < 1e-9);
        EXPECT(fabs(samples[1].elapsed_seconds - 1.0) < 1e-9);
        EXPECT(fabs(samples[2].elapsed_seconds - 3.0) < 1e-9);
        EXPECT(fabs(samples[2].t1 - 1.2) < 1e-9);
    }
    replay_samples_free(samples);
    remove(path);
}

static void test_csv_and_simple(void)
{
    static const char fixture[] =
        "timestamp,elapsed_s,t1_c,t2_c,delta_t_c,valid\n"
        "2026-09-24T18:30:00.000,0.000,24.5,24.6,-0.1,1\n"
        "2026-09-24T18:30:01.000,1.000,,,0\n"
        "2026-09-24T18:30:02.000,2.500,25.0,24.0,1.0,1\n"
        "\n"
        "4.0,26.0,25.5\n";
    char path[512];
    char error[128];
    ReplaySample *samples = NULL;
    size_t count = 0;

    EXPECT(write_temp(path, sizeof path, fixture) == 0);
    EXPECT(replay_parse_path(path, &samples, &count, NULL, error, sizeof error) == 0);
    EXPECT(count == 4u);
    if (count == 4u && samples != NULL) {
        EXPECT(samples[0].valid);
        EXPECT(fabs(samples[0].t1 - 24.5) < 1e-9);
        EXPECT(fabs(samples[0].t2 - 24.6) < 1e-9);
        EXPECT(!samples[1].valid);
        EXPECT(!isfinite(samples[1].t1));
        EXPECT(!isfinite(samples[1].t2));
        EXPECT(samples[1].t1 != 0.0);
        EXPECT(fabs(samples[1].elapsed_seconds - 1.0) < 1e-9);
        EXPECT(samples[2].valid);
        EXPECT(fabs(samples[2].elapsed_seconds - 2.5) < 1e-9);
        EXPECT(fabs(samples[3].elapsed_seconds - 4.0) < 1e-9);
        EXPECT(fabs(samples[3].t2 - 25.5) < 1e-9);
    }
    replay_samples_free(samples);
    remove(path);
}

static void test_header_only_is_rejected(void)
{
    static const char fixture[] = "File Name:\tonly.txt\nDescription:\n";
    char path[512];
    char error[128];
    ReplaySample *samples = NULL;
    size_t count = 99;

    EXPECT(write_temp(path, sizeof path, fixture) == 0);
    EXPECT(replay_parse_path(path, &samples, &count, NULL, error, sizeof error) != 0);
    EXPECT(samples == NULL);
    EXPECT(count == 0u);
    EXPECT(error[0] != '\0');
    remove(path);
}

static void test_shipped_example(void)
{
#ifdef TMD_EXAMPLE_PATH
    char error[256];
    ReplaySample *samples = NULL;
    size_t count = 0;
    size_t i;
    int invalid = 0;

    EXPECT(replay_parse_path(TMD_EXAMPLE_PATH, &samples, &count, NULL, error, sizeof error) == 0);
    EXPECT(count >= 10u);
    if (count > 0u && samples != NULL) {
        EXPECT(fabs(samples[0].t1 - 24.4) < 1e-9);
        EXPECT(fabs(samples[0].t2 - 24.8) < 1e-9);
        EXPECT(samples[0].valid);
    }
    for (i = 0; i < count; i++) {
        if (!samples[i].valid) {
            invalid++;
            EXPECT(!isfinite(samples[i].t1) || !isfinite(samples[i].t2));
        }
    }
    EXPECT(invalid >= 1);
    replay_samples_free(samples);
#else
    fprintf(stderr, "FAIL example path was not compiled in\n");
    failures++;
#endif
}

static int load_fixture(const char *name, ReplaySample **samples, size_t *count,
                          ReplayParseReport *report, char *error, size_t error_len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", TMD_FIXTURE_DIR, name);
    return replay_parse_path(path, samples, count, report, error, error_len);
}

static void test_fixtures(void)
{
    char error[256];
    ReplayParseReport report;
    ReplaySample *samples = NULL;
    size_t count = 0;

    EXPECT(load_fixture("normal_export.txt", &samples, &count, &report, error, sizeof error) == 0);
    EXPECT(count == 3u);
    EXPECT(report.skipped == 0u);
    if (count == 3u && samples != NULL) {
        EXPECT(fabs(samples[0].elapsed_seconds) < 1e-9);
        EXPECT(fabs(samples[0].t1 - 24.4) < 1e-9);
        EXPECT(fabs(samples[2].elapsed_seconds - 2.0) < 1e-9);
        EXPECT(samples[2].valid);
    }
    replay_samples_free(samples);

    samples = NULL;
    count = 0;
    EXPECT(load_fixture("metadata_before_data.txt", &samples, &count, &report, error,
                        sizeof error) == 0);
    EXPECT(count == 2u);
    EXPECT(report.skipped == 0u);
    if (count == 2u && samples != NULL) {
        EXPECT(fabs(samples[0].elapsed_seconds) < 1e-9);
        EXPECT(fabs(samples[0].t1 - 18.0) < 1e-9);
        EXPECT(fabs(samples[1].elapsed_seconds - 2.0) < 1e-9);
    }
    replay_samples_free(samples);

    samples = NULL;
    count = 0;
    EXPECT(load_fixture("malformed_row.txt", &samples, &count, &report, error, sizeof error) == 0);
    EXPECT(count == 2u);
    EXPECT(report.skipped == 1u);
    EXPECT(strstr(report.summary, "Loaded 2 samples.") != NULL);
    EXPECT(strstr(report.summary, "Skipped 1 malformed") != NULL);
    if (count == 2u && samples != NULL) {
        EXPECT(fabs(samples[1].t1 - 20.2) < 1e-9);
        EXPECT(fabs(samples[1].elapsed_seconds - 1.0) < 1e-9);
    }
    replay_samples_free(samples);

    samples = NULL;
    count = 0;
    EXPECT(load_fixture("missing_value.txt", &samples, &count, &report, error, sizeof error) == 0);
    EXPECT(count == 3u);
    EXPECT(report.skipped == 0u);
    if (count == 3u && samples != NULL) {
        EXPECT(samples[0].valid);
        EXPECT(!samples[1].valid);
        EXPECT(!isfinite(samples[1].t1));
        EXPECT(fabs(samples[1].t2 - 23.2) < 1e-9);
        EXPECT(samples[1].t1 != 0.0);
        EXPECT(!samples[2].valid);
        EXPECT(!isfinite(samples[2].t1));
        EXPECT(fabs(samples[2].t2 - 23.4) < 1e-9);
        EXPECT(fabs(samples[2].elapsed_seconds - 2.0) < 1e-9);
    }
    replay_samples_free(samples);

    samples = NULL;
    count = 0;
    EXPECT(load_fixture("irregular_timestamps.txt", &samples, &count, &report, error,
                        sizeof error) == 0);
    EXPECT(count == 3u);
    if (count == 3u && samples != NULL) {
        EXPECT(fabs(samples[0].elapsed_seconds) < 1e-9);
        EXPECT(fabs(samples[1].elapsed_seconds - 0.5) < 1e-9);
        EXPECT(fabs(samples[2].elapsed_seconds - 2.5) < 1e-9);
        EXPECT(fabs((samples[2].elapsed_seconds - samples[1].elapsed_seconds) - 2.0) < 1e-9);
    }
    replay_samples_free(samples);

    samples = NULL;
    count = 0;
    EXPECT(load_fixture("duplicate_timestamps.txt", &samples, &count, &report, error,
                        sizeof error) == 0);
    EXPECT(count == 3u);
    EXPECT(report.duplicate_timestamps == 1u);
    if (count == 3u && samples != NULL) {
        EXPECT(fabs(samples[0].elapsed_seconds) < 1e-9);
        EXPECT(fabs(samples[1].elapsed_seconds) < 1e-9);
        EXPECT(fabs(samples[1].t1 - 10.4) < 1e-9);
        EXPECT(fabs(samples[2].elapsed_seconds - 2.0) < 1e-9);
    }
    replay_samples_free(samples);

    samples = NULL;
    count = 0;
    EXPECT(load_fixture("negative_temperatures.txt", &samples, &count, &report, error,
                        sizeof error) == 0);
    EXPECT(count == 2u);
    if (count == 2u && samples != NULL) {
        EXPECT(samples[0].valid);
        EXPECT(samples[1].valid);
        EXPECT(samples[0].t1 < 0.0);
        EXPECT(samples[0].t2 < 0.0);
        EXPECT(fabs(samples[0].t1 - (-196.4)) < 1e-9);
        EXPECT(fabs(samples[0].elapsed_seconds) < 1e-9);
        EXPECT(fabs(samples[1].elapsed_seconds - 1.5) < 1e-9);
        EXPECT(fabs(samples[1].t2 - (-180.2)) < 1e-9);
    }
    replay_samples_free(samples);

    samples = NULL;
    count = 0;
    EXPECT(load_fixture("decreasing_timestamps.txt", &samples, &count, &report, error,
                        sizeof error) == 0);
    EXPECT(count == 4u);
    EXPECT(report.backward_timestamps == 1u);
    if (count == 4u && samples != NULL) {
        EXPECT(fabs(samples[0].elapsed_seconds) < 1e-9);
        EXPECT(fabs(samples[1].elapsed_seconds - 5.0) < 1e-9);
        EXPECT(fabs(samples[2].elapsed_seconds - 5.0) < 1e-9);
        EXPECT(fabs(samples[2].t1 - 1.8) < 1e-9);
        EXPECT(fabs(samples[3].elapsed_seconds - 8.0) < 1e-9);
    }
    replay_samples_free(samples);
}

static void test_missing_columns(void)
{
    char path[512];
    char error[128];
    ReplaySample *samples = NULL;
    size_t count = 0;

    EXPECT(write_temp(path, sizeof path,
                      "No.\tDate\tTime\tCH01\n"
                      "1\t2026/01/01\t00:00:00\t1.0\n") == 0);
    EXPECT(replay_parse_path(path, &samples, &count, NULL, error, sizeof error) != 0);
    EXPECT(strstr(error, "T2") != NULL);
    remove(path);

    EXPECT(write_temp(path, sizeof path,
                      "No.\tCH01\tCH02\n"
                      "1\t1.0\t2.0\n") == 0);
    EXPECT(replay_parse_path(path, &samples, &count, NULL, error, sizeof error) != 0);
    EXPECT(strstr(error, "time") != NULL);
    remove(path);
}

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void test_playback_timing_restart_and_completion(void)
{
    static const char fixture[] =
        "elapsed_s,t1_c,t2_c\n"
        "0.0,20.0,-1.0\n"
        "0.30,20.5,-1.5\n"
        "0.60,21.0,-2.0\n";
    char path[512];
    ReplaySource *source;
    MeasurementSource *base;
    TemperatureMeasurement sample;
    double started;
    int got = 0;
    int second_ok = 0;

    EXPECT(write_temp(path, sizeof path, fixture) == 0);
    source = replay_source_create();
    EXPECT(source != NULL);
    if (source == NULL) {
        remove(path);
        return;
    }
    base = replay_source_base(source);
    EXPECT(measurement_source_open(base, path) == 0);
    EXPECT(replay_source_speed(source) == 1.0);
    replay_source_set_speed(source, 2.0);
    EXPECT(measurement_source_start(base) == 0);
    EXPECT(measurement_source_get(base, &sample) == 1);
    EXPECT(fabs(sample.elapsed_seconds) < 1e-6);
    EXPECT(sample.t2 < 0.0);
    EXPECT(sample.valid);
    started = tmd_monotonic_seconds();
    while (tmd_monotonic_seconds() - started < 1.2 && !measurement_source_finished(base)) {
        if (measurement_source_get(base, &sample) == 1) {
            got++;
            if (got == 1) {
                double waited = tmd_monotonic_seconds() - started;
                second_ok = waited > 0.08 && waited < 0.45 &&
                            fabs(sample.elapsed_seconds - 0.30) < 1e-6;
            }
        } else {
            sleep_ms(20);
        }
    }
    EXPECT(second_ok);
    EXPECT(got >= 2);
    EXPECT(measurement_source_finished(base));
    EXPECT(measurement_source_get(base, &sample) == 0);

    replay_source_restart(source);
    EXPECT(!replay_source_is_finished(source));
    EXPECT(measurement_source_start(base) == 0);
    EXPECT(measurement_source_get(base, &sample) == 1);
    EXPECT(fabs(sample.elapsed_seconds) < 1e-6);
    EXPECT(fabs(sample.t1 - 20.0) < 1e-9);

    replay_source_destroy(source);
    remove(path);
}

int main(void)
{
    test_tmd_export();
    test_midnight_wrap();
    test_csv_and_simple();
    test_header_only_is_rejected();
    test_shipped_example();
    test_fixtures();
    test_missing_columns();
    test_playback_timing_restart_and_completion();
    if (failures != 0) {
        fprintf(stderr, "%d replay parser check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("replay parser tests passed\n");
    return EXIT_SUCCESS;
}
