#include "self_test.h"

#include "history_buffer.h"
#include "platform/user_paths.h"
#include "sources/simulator_source.h"
#include "version.h"

#include <gio/gio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int resource_ok(const char *path)
{
    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    int ok = bytes != NULL && g_bytes_get_size(bytes) > 0u;
    if (bytes != NULL) {
        g_bytes_unref(bytes);
    }
    if (error != NULL) {
        fprintf(stderr, "tmd56: %s: %s\n", path, error->message);
        g_error_free(error);
    }
    return ok;
}

static int check_resources(void)
{
    int ok = resource_ok("/com/tmd56/Monitor/style.css") &&
             resource_ok("/com/tmd56/Monitor/icons/tmd56-monitor.svg") &&
             resource_ok("/com/tmd56/Monitor/icons/tmd56-monitor-128.png");
    printf("Resources: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int check_user_data(void)
{
    const char *logs = user_logs_dir();
    int ok = logs != NULL && logs[0] != '\0';
    printf("User data directory: %s\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        fprintf(stderr, "tmd56: user data directory was not resolved\n");
    }
    return ok;
}

static int check_logging_dir(void)
{
    char error[128];
    const char *logs;
    int ok;

    error[0] = '\0';
    ok = user_logs_ensure(error, sizeof error) == 0;
    logs = user_logs_dir();
    if (ok && (logs == NULL || !g_file_test(logs, G_FILE_TEST_IS_DIR))) {
        ok = 0;
    }
    printf("Logging directory: %s\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        fprintf(stderr, "tmd56: %s\n", error[0] != '\0' ? error : "could not create the log directory");
    }
    return ok;
}

static int check_simulator(void)
{
    SimulatorSource *simulator = simulator_source_create();
    MeasurementSource *source;
    TemperatureMeasurement sample;
    int rc;
    int ok = 0;

    if (simulator == NULL) {
        fprintf(stderr, "tmd56: simulator allocation failed\n");
        printf("Simulator: FAIL\n");
        return 0;
    }
    source = simulator_source_base(simulator);
    if (measurement_source_open(source, NULL) == 0 && measurement_source_start(source) == 0) {
        rc = measurement_source_get(source, &sample);
        ok = rc == 1 && sample.valid && isfinite(sample.t1) && isfinite(sample.t2) &&
             isfinite(sample.delta_t);
        if (!ok) {
            fprintf(stderr, "tmd56: simulator did not produce a finite sample (rc %d)\n", rc);
        }
    } else {
        fprintf(stderr, "tmd56: simulator did not start\n");
    }
    measurement_source_stop(source);
    simulator_source_destroy(simulator);
    printf("Simulator: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int check_core(void)
{
    HistoryBuffer history;
    TemperatureMeasurement sample;
    const TemperatureMeasurement *stored;
    int ok = 0;

    if (history_buffer_init(&history) != 0) {
        fprintf(stderr, "tmd56: history buffer allocation failed\n");
        printf("Core initialization: FAIL\n");
        return 0;
    }
    measurement_set(&sample, 1.5, -2.5, 0.0, true);
    if (history_buffer_push(&history, &sample) == 0) {
        stored = history_buffer_at(&history, 0);
        ok = stored != NULL && stored->valid && fabs(stored->t1 - 1.5) < 1e-9 &&
             fabs(stored->t2 - (-2.5)) < 1e-9 && fabs(stored->delta_t - 4.0) < 1e-9;
    }
    history_buffer_free(&history);
    if (!ok) {
        fprintf(stderr, "tmd56: history round-trip failed\n");
    }
    printf("Core initialization: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

int tmd_self_test(void)
{
    int ok = 1;

    if (TMD_VERSION_STRING[0] == '\0') {
        fprintf(stderr, "tmd56: version string is empty\n");
        ok = 0;
    }
    printf("TMD-56 Temperature Logger %s\n", TMD_VERSION_STRING);
    if (!check_resources()) {
        ok = 0;
    }
    if (!check_user_data()) {
        ok = 0;
    }
    if (!check_logging_dir()) {
        ok = 0;
    }
    if (!check_simulator()) {
        ok = 0;
    }
    if (!check_core()) {
        ok = 0;
    }
    printf("\nSELF TEST: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
