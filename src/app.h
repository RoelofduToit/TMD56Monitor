#ifndef TMD56_APP_H
#define TMD56_APP_H

#include "history_buffer.h"
#include "measurement.h"
#include "sources/simulator_source.h"

#include <stddef.h>

typedef struct App App;

typedef enum {
    APP_SOURCE_SIMULATOR = 0,
    APP_SOURCE_REPLAY,
    APP_SOURCE_TMD56
} AppSourceKind;

typedef enum {
    APP_LEVEL_IDLE = 0,
    APP_LEVEL_RUNNING,
    APP_LEVEL_WARNING,
    APP_LEVEL_ERROR
} AppStatusLevel;

App *app_create(void);
void app_destroy(App *app);

void app_set_source(App *app, AppSourceKind kind);
AppSourceKind app_source(const App *app);

void app_set_scenario(App *app, SimulatorScenario scenario);
void app_set_sample_interval(App *app, double seconds);
void app_set_replay_speed(App *app, double speed);
int app_set_replay_file(App *app, const char *path, char *err, size_t err_len);
void app_set_serial_port(App *app, const char *port);

int app_start(App *app, char *err, size_t err_len);
void app_stop(App *app);
int app_poll(App *app);

/* Keeps a concise fault on the status line until a later action replaces it. */
void app_report_error(App *app, const char *text);

void app_replay_pause(App *app, bool paused);
bool app_replay_paused(const App *app);
void app_replay_restart(App *app);

int app_start_logging(App *app, const char *session_name, char *err, size_t err_len);
void app_stop_logging(App *app);

void app_set_spike_filter(App *app, bool enabled);
void app_set_spike_threshold(App *app, double threshold_c);

bool app_is_acquiring(const App *app);
bool app_is_logging(const App *app);
AppStatusLevel app_status_level(const App *app);
const char *app_status(const App *app);
const char *app_log_path(const App *app);
const HistoryBuffer *app_history(const App *app);
const MeasurementStats *app_stats(const App *app);
bool app_has_latest(const App *app);
const TemperatureMeasurement *app_latest(const App *app);
bool app_spike_filter_enabled(const App *app);
size_t app_replay_sample_count(const App *app);
const char *app_replay_path(const App *app);
const char *app_replay_summary(const App *app);
bool app_replay_finished(const App *app);

#endif
