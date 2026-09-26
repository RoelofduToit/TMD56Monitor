#define _POSIX_C_SOURCE 200809L

#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

static void expect_true(int condition, const char *file, int line, const char *text)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s:%d %s\n", file, line, text);
        failures++;
    }
}

#define EXPECT(condition) expect_true((condition), __FILE__, __LINE__, #condition)

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static size_t wait_for_samples(App *app, size_t target, int timeout_ms)
{
    int waited = 0;
    while (history_buffer_count(app_history(app)) < target && waited < timeout_ms) {
        app_poll(app);
        sleep_ms(20);
        waited += 20;
    }
    app_poll(app);
    return history_buffer_count(app_history(app));
}

static void expect_series(const HistoryBuffer *history, size_t from, size_t to, double period)
{
    size_t i;
    for (i = from + 1u; i < to; i++) {
        const TemperatureMeasurement *sample = history_buffer_at(history, i);
        const TemperatureMeasurement *previous = history_buffer_at(history, i - 1u);
        double gap = sample->elapsed_seconds - previous->elapsed_seconds;
        char t1[16];
        char t2[16];
        char delta[16];

        if (fabs(gap - period) >= 0.002) {
            fprintf(stderr, "spacing %.6f, expected %.3f at sample %zu\n", gap, period, i);
        }
        EXPECT(fabs(gap - period) < 0.002);
        EXPECT(gap > 0.0);
        EXPECT(isfinite(sample->t1));
        EXPECT(isfinite(sample->t2));
        EXPECT(isfinite(sample->delta_t));
        EXPECT(isfinite(sample->elapsed_seconds));
        EXPECT(fabs(sample->delta_t - (sample->t1 - sample->t2)) < 1e-9);
        EXPECT(fabs(sample->t1 - previous->t1) < 0.5);
        EXPECT(fabs(sample->t2 - previous->t2) < 0.5);
        measurement_format_triple(t1, sizeof t1, t2, sizeof t2, delta, sizeof delta,
                                  sample->t1, sample->t2, sample->valid);
        EXPECT(strstr(t1, "nan") == NULL && strstr(t1, "inf") == NULL);
        EXPECT(strstr(t2, "nan") == NULL && strstr(t2, "inf") == NULL);
        EXPECT(strstr(delta, "nan") == NULL && strstr(delta, "inf") == NULL);
        EXPECT(strstr(delta, "-0.0") == NULL);
    }
}

static char *read_all(const char *path)
{
    FILE *file = fopen(path, "r");
    char *buf;
    long length;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    buf = malloc((size_t)length + 1u);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(buf, 1, (size_t)length, file) != (size_t)length) {
        free(buf);
        fclose(file);
        return NULL;
    }
    buf[length] = '\0';
    fclose(file);
    return buf;
}

static int count_text(const char *text, const char *needle)
{
    int count = 0;
    const char *found = text;
    if (text == NULL || needle == NULL) {
        return 0;
    }
    while ((found = strstr(found, needle)) != NULL) {
        count++;
        found += strlen(needle);
    }
    return count;
}

int main(void)
{
    char dir[] = "/tmp/tmd56-acq-XXXXXX";
    char err[256];
    App *app;
    const HistoryBuffer *history;
    size_t count;
    size_t mark;
    size_t stopped;
    const TemperatureMeasurement *latest;
    char saved[768];
    char *text;
    int cycle;

    if (mkdtemp(dir) == NULL || chdir(dir) != 0) {
        fprintf(stderr, "FAIL could not create a temporary directory\n");
        return EXIT_FAILURE;
    }
    setenv("TMD56_DATA_DIR", dir, 1);

    app = app_create();
    EXPECT(app != NULL);
    if (app == NULL) {
        return EXIT_FAILURE;
    }

    EXPECT(!app_is_acquiring(app));
    EXPECT(!app_is_logging(app));
    EXPECT(app_start_logging(app, "session", err, sizeof err) != 0);
    EXPECT(!app_is_logging(app));
    EXPECT(strstr(app_status(app), "acquisition") != NULL);

    app_set_sample_interval(app, 0.25);
    EXPECT(app_start(app, err, sizeof err) == 0);
    EXPECT(app_is_acquiring(app));
    count = wait_for_samples(app, 2u, 1500);
    EXPECT(count >= 2u && count <= 4u);
    EXPECT(app_start(app, err, sizeof err) == 0);
    EXPECT(history_buffer_count(app_history(app)) >= 2u);
    EXPECT(fabs(history_buffer_at(app_history(app), 0)->elapsed_seconds) < 0.001);
    count = wait_for_samples(app, 4u, 2000);
    EXPECT(count >= 4u && count <= 6u);
    history = app_history(app);
    latest = history_buffer_at(history, 0);
    EXPECT(latest != NULL);
    EXPECT(fabs(latest->t1 - 25.0) < 1.0);
    EXPECT(fabs(latest->t2 - 25.0) < 1.0);
    expect_series(history, 0u, count, 0.25);

    mark = history_buffer_count(app_history(app));
    app_set_sample_interval(app, 0.5);
    count = wait_for_samples(app, mark + 3u, 3000);
    EXPECT(count >= mark + 3u);
    expect_series(app_history(app), mark, count, 0.5);

    stopped = history_buffer_count(app_history(app));
    app_stop(app);
    EXPECT(!app_is_acquiring(app));
    EXPECT(app_poll(app) == 0);
    sleep_ms(350);
    EXPECT(app_poll(app) == 0);
    EXPECT(history_buffer_count(app_history(app)) == stopped);

    /* The interval change above left the period at 0.5 s. */
    app_set_sample_interval(app, 0.25);
    for (cycle = 0; cycle < 3; cycle++) {
        size_t began;
        EXPECT(app_start(app, err, sizeof err) == 0);
        began = wait_for_samples(app, 2u, 1500);
        EXPECT(began >= 2u && began <= 4u);
        EXPECT(fabs(history_buffer_at(app_history(app), 0)->elapsed_seconds) < 0.001);
        expect_series(app_history(app), 0u, began, 0.25);
        app_stop(app);
        stopped = history_buffer_count(app_history(app));
        sleep_ms(200);
        EXPECT(app_poll(app) == 0);
        EXPECT(history_buffer_count(app_history(app)) == stopped);
        EXPECT(!app_is_logging(app));
    }

    EXPECT(app_start(app, err, sizeof err) == 0);
    EXPECT(app_start_logging(app, "   ", err, sizeof err) != 0);
    EXPECT(!app_is_logging(app));
    EXPECT(app_poll(app) == 0 || app_is_acquiring(app));
    app_poll(app);
    EXPECT(app_is_acquiring(app));
    EXPECT(strstr(app_status(app), "session name") != NULL);
    EXPECT(app_status_level(app) == APP_LEVEL_ERROR);

    EXPECT(app_start_logging(app, "bad/name", err, sizeof err) == 0);
    EXPECT(app_is_logging(app));
    EXPECT(strstr(app_log_path(app), "bad/name") == NULL);
    EXPECT(strstr(app_log_path(app), "bad_name_") != NULL);
    EXPECT(app_status_level(app) != APP_LEVEL_ERROR);
    snprintf(saved, sizeof saved, "%s", app_log_path(app));
    EXPECT(app_start_logging(app, "bad/name", err, sizeof err) != 0);
    EXPECT(strcmp(app_log_path(app), saved) == 0);
    wait_for_samples(app, 2u, 1500);
    app_stop_logging(app);
    EXPECT(!app_is_logging(app));
    EXPECT(app_is_acquiring(app));
    text = read_all(saved);
    EXPECT(text != NULL);
    if (text != NULL) {
        EXPECT(count_text(text, "timestamp,elapsed_s,t1_c,t2_c,delta_t_c,valid\n") == 1);
        EXPECT(strchr(text, '\n') != NULL && strchr(strchr(text, '\n') + 1, '\n') != NULL);
        EXPECT(strstr(text, ".000,") != NULL || strstr(text, ".250,") != NULL ||
               strstr(text, ".500,") != NULL || strstr(text, ".750,") != NULL);
        EXPECT(strstr(text, "nan") == NULL);
        EXPECT(strstr(text, "inf") == NULL);
        free(text);
    }
    EXPECT(app_start_logging(app, "bad/name", err, sizeof err) == 0);
    EXPECT(strcmp(app_log_path(app), saved) != 0);
    app_set_source(app, APP_SOURCE_REPLAY);
    EXPECT(app_is_acquiring(app));
    EXPECT(app_source(app) == APP_SOURCE_SIMULATOR);
    EXPECT(strstr(app_status(app), "Stop before changing") != NULL);
    app_stop(app);
    EXPECT(!app_is_acquiring(app));
    EXPECT(!app_is_logging(app));
    app_set_source(app, APP_SOURCE_REPLAY);
    text = read_all(saved);
    EXPECT(text != NULL && count_text(text, "timestamp,elapsed_s,t1_c,t2_c,delta_t_c,valid\n") == 1);
    free(text);

    {
        FILE *junk = fopen("junk.txt", "w");
        EXPECT(junk != NULL);
        if (junk != NULL) {
            fputs("this is not a measurement\n", junk);
            fclose(junk);
        }
    }
    EXPECT(app_set_replay_file(app, "junk.txt", err, sizeof err) != 0);
    EXPECT(strstr(app_status(app), "No measurements") != NULL);
    EXPECT(app_start(app, err, sizeof err) != 0);
    EXPECT(!app_is_acquiring(app));
    EXPECT(app_poll(app) == 0);
    EXPECT(app_set_replay_file(app, "missing-replay.csv", err, sizeof err) != 0);
    EXPECT(strstr(app_status(app), "Could not open") != NULL);

    {
        FILE *good = fopen("replay.csv", "w");
        EXPECT(good != NULL);
        if (good != NULL) {
            fputs("elapsed_s,t1_c,t2_c\n0.0,-4.5,-5.5\n0.4,-4.0,-5.0\n", good);
            fclose(good);
        }
    }
    EXPECT(app_set_replay_file(app, "replay.csv", err, sizeof err) == 0);
    EXPECT(strstr(app_replay_summary(app), "Loaded 2 samples.") != NULL);
    EXPECT(app_start_logging(app, "replay-log", err, sizeof err) != 0);
    EXPECT(!app_is_logging(app));
    EXPECT(strstr(app_status(app), "Replay") != NULL);
    EXPECT(app_start(app, err, sizeof err) == 0);
    EXPECT(wait_for_samples(app, 1u, 1000) >= 1u);
    EXPECT(app_latest(app) != NULL && app_latest(app)->t1 < 0.0);
    app_replay_restart(app);
    EXPECT(app_is_acquiring(app));
    EXPECT(history_buffer_count(app_history(app)) == 0u);
    EXPECT(wait_for_samples(app, 1u, 1000) >= 1u);
    EXPECT(fabs(history_buffer_at(app_history(app), 0)->elapsed_seconds) < 1e-6);
    EXPECT(history_buffer_at(app_history(app), 0)->t2 < 0.0);
    app_set_source(app, APP_SOURCE_SIMULATOR);
    EXPECT(app_source(app) == APP_SOURCE_REPLAY);
    EXPECT(app_is_acquiring(app));
    app_stop(app);
    EXPECT(!app_is_acquiring(app));

    app_set_source(app, APP_SOURCE_SIMULATOR);
    EXPECT(app_start(app, err, sizeof err) == 0);
    EXPECT(app_start_logging(app, "final", err, sizeof err) == 0);
    wait_for_samples(app, 1u, 1000);
    app_stop(app);
    EXPECT(!app_is_acquiring(app));
    EXPECT(!app_is_logging(app));

    app_destroy(app);
    if (failures != 0) {
        fprintf(stderr, "%d acquisition check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("acquisition tests passed\n");
    return EXIT_SUCCESS;
}
