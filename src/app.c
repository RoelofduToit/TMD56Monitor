#include "app.h"

#include "logger.h"
#include "platform/user_paths.h"
#include "sources/replay_source.h"
#include "sources/tmd56_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct App {
    AppSourceKind kind;
    AppStatusLevel level;
    bool acquiring;
    bool spike_filter_enabled;
    bool log_fault;
    double spike_threshold;
    char status[384];
    char serial_port[256];

    SimulatorSource *simulator;
    ReplaySource *replay;
    Tmd56Source *tmd56;
    MeasurementSource *active;

    HistoryBuffer history;
    MeasurementStats stats;
    SpikeTracker spikes;
    bool have_latest;
    TemperatureMeasurement latest;

    Logger *logger;
};

static void set_status(App *app, const char *text, AppStatusLevel level)
{
    snprintf(app->status, sizeof app->status, "%s", text != NULL ? text : "");
    app->level = level;
}

void app_report_error(App *app, const char *text)
{
    if (app == NULL) {
        return;
    }
    if (text == NULL || text[0] == '\0') {
        text = "Something went wrong";
    }
    fprintf(stderr, "tmd56: %s\n", text);
    set_status(app, text, APP_LEVEL_ERROR);
}

static void recompute_stats(App *app)
{
    size_t i;
    size_t count;
    bool ignore = app->spike_filter_enabled && app->kind != APP_SOURCE_TMD56;

    stats_reset(&app->stats);
    count = history_buffer_count(&app->history);
    for (i = 0; i < count; i++) {
        stats_accumulate(&app->stats, history_buffer_at(&app->history, i), ignore);
    }
    app->have_latest = app->stats.have_latest;
    if (app->have_latest) {
        app->latest = app->stats.latest;
    }
}

static void remember_latest(App *app, const TemperatureMeasurement *sample)
{
    app->latest = *sample;
    app->have_latest = true;
}

static void clear_tail_marks(App *app, int unmark)
{
    size_t count = history_buffer_count(&app->history);
    size_t nclear = (size_t)unmark;
    size_t from;
    size_t j;

    if (unmark <= 0 || count == 0) {
        return;
    }
    if (nclear > count) {
        nclear = count;
    }
    from = count - nclear;
    for (j = from; j < count; j++) {
        history_buffer_at_mut(&app->history, j)->suspicious = false;
    }
}

static void close_log(App *app)
{
    if (logger_is_open(app->logger)) {
        logger_close(app->logger);
    }
}

static void halt(App *app, const char *text, AppStatusLevel level)
{
    if (app->acquiring && app->active != NULL) {
        measurement_source_stop(app->active);
        if (app->kind == APP_SOURCE_TMD56) {
            measurement_source_close(app->active);
        }
    }
    close_log(app);
    app->acquiring = false;
    set_status(app, text, level);
}

static void idle_status_for_source(App *app)
{
    if (app->kind == APP_SOURCE_TMD56) {
        set_status(app,
                   "Experimental / unverified — temperature decoding is not implemented.",
                   APP_LEVEL_WARNING);
        return;
    }
    if (app->kind == APP_SOURCE_REPLAY) {
        const char *text = measurement_source_status(replay_source_base(app->replay));
        set_status(app, text, APP_LEVEL_IDLE);
        return;
    }
    set_status(app, "Idle — simulator ready", APP_LEVEL_IDLE);
}

static MeasurementSource *source_for(App *app, AppSourceKind kind)
{
    if (kind == APP_SOURCE_REPLAY) {
        return replay_source_base(app->replay);
    }
    if (kind == APP_SOURCE_TMD56) {
        return tmd56_source_base(app->tmd56);
    }
    return simulator_source_base(app->simulator);
}

App *app_create(void)
{
    App *app = calloc(1, sizeof(*app));
    if (app == NULL) {
        return NULL;
    }
    if (history_buffer_init(&app->history) != 0) {
        free(app);
        return NULL;
    }
    app->logger = logger_create();
    app->simulator = simulator_source_create();
    app->replay = replay_source_create();
    app->tmd56 = tmd56_source_create();
    if (app->logger == NULL || app->simulator == NULL ||
        app->replay == NULL || app->tmd56 == NULL) {
        app_destroy(app);
        return NULL;
    }
    app->kind = APP_SOURCE_SIMULATOR;
    app->active = simulator_source_base(app->simulator);
    app->spike_threshold = TMD_DEFAULT_SPIKE_THRESHOLD_C;
    stats_reset(&app->stats);
    spike_tracker_reset(&app->spikes);
    idle_status_for_source(app);
    return app;
}

void app_destroy(App *app)
{
    if (app == NULL) {
        return;
    }
    if (app->acquiring && app->active != NULL) {
        measurement_source_stop(app->active);
        if (app->kind == APP_SOURCE_TMD56) {
            measurement_source_close(app->active);
        }
    }
    logger_destroy(app->logger);
    simulator_source_destroy(app->simulator);
    replay_source_destroy(app->replay);
    tmd56_source_destroy(app->tmd56);
    history_buffer_free(&app->history);
    free(app);
}

void app_set_source(App *app, AppSourceKind kind)
{
    if (app == NULL) {
        return;
    }
    if (kind != APP_SOURCE_SIMULATOR && kind != APP_SOURCE_REPLAY &&
        kind != APP_SOURCE_TMD56) {
        kind = APP_SOURCE_SIMULATOR;
    }
    if (app->acquiring) {
        if (kind != app->kind) {
            fprintf(stderr, "tmd56: source change refused while acquisition is running\n");
            set_status(app, "Stop before changing the source", APP_LEVEL_ERROR);
        }
        return;
    }
    app->kind = kind;
    app->active = source_for(app, kind);
    idle_status_for_source(app);
}

AppSourceKind app_source(const App *app)
{
    return app->kind;
}

void app_set_scenario(App *app, SimulatorScenario scenario)
{
    if (app == NULL) {
        return;
    }
    simulator_source_set_scenario(app->simulator, scenario);
    if (app->kind == APP_SOURCE_SIMULATOR && !app->acquiring) {
        idle_status_for_source(app);
    }
}

void app_set_sample_interval(App *app, double seconds)
{
    if (app == NULL) {
        return;
    }
    simulator_source_set_period(app->simulator, seconds);
}

void app_set_replay_speed(App *app, double speed)
{
    if (app == NULL) {
        return;
    }
    replay_source_set_speed(app->replay, speed);
    if (app->kind == APP_SOURCE_REPLAY && app->acquiring) {
        set_status(app, measurement_source_status(replay_source_base(app->replay)),
                   APP_LEVEL_RUNNING);
    }
}

int app_set_replay_file(App *app, const char *path, char *err, size_t err_len)
{
    int rc;
    if (app == NULL) {
        return -1;
    }
    if (app->acquiring && app->kind == APP_SOURCE_REPLAY) {
        halt(app, "Stopped", APP_LEVEL_IDLE);
    }
    rc = measurement_source_open(replay_source_base(app->replay), path);
    if (rc != 0) {
        const char *text = measurement_source_status(replay_source_base(app->replay));
        fprintf(stderr, "tmd56: replay open failed: %s\n", text);
        set_status(app, text, APP_LEVEL_ERROR);
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "%s", text);
        }
        return -1;
    }
    if (app->kind == APP_SOURCE_REPLAY) {
        idle_status_for_source(app);
    }
    if (err != NULL && err_len > 0) {
        err[0] = '\0';
    }
    return 0;
}

void app_set_serial_port(App *app, const char *port)
{
    if (app == NULL) {
        return;
    }
    snprintf(app->serial_port, sizeof app->serial_port, "%s", port != NULL ? port : "");
}

int app_start(App *app, char *err, size_t err_len)
{
    int rc;
    if (app == NULL || app->active == NULL) {
        return -1;
    }
    if (app->acquiring) {
        return 0;
    }

    if (app->kind == APP_SOURCE_SIMULATOR) {
        measurement_source_open(app->active, NULL);
        rc = measurement_source_start(app->active);
    } else if (app->kind == APP_SOURCE_REPLAY) {
        rc = measurement_source_start(app->active);
    } else {
        if (app->serial_port[0] == '\0') {
            set_status(app,
                       "Enter a serial port. Experimental / unverified — no readings will be invented.",
                       APP_LEVEL_ERROR);
            if (err != NULL && err_len > 0) {
                snprintf(err, err_len, "%s", app->status);
            }
            return -1;
        }
        rc = measurement_source_open(app->active, app->serial_port);
        if (rc == 0) {
            rc = measurement_source_start(app->active);
            if (rc != 0) {
                measurement_source_close(app->active);
            }
        }
    }

    if (rc != 0) {
        const char *text = measurement_source_status(app->active);
        fprintf(stderr, "tmd56: acquisition did not start: %s\n", text);
        set_status(app, text, APP_LEVEL_ERROR);
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "%s", text);
        }
        return -1;
    }

    history_buffer_clear(&app->history);
    stats_reset(&app->stats);
    spike_tracker_reset(&app->spikes);
    app->have_latest = false;
    measurement_clear(&app->latest);
    app->log_fault = false;
    app->acquiring = true;
    set_status(app, measurement_source_status(app->active),
               app->kind == APP_SOURCE_TMD56 ? APP_LEVEL_WARNING : APP_LEVEL_RUNNING);
    if (err != NULL && err_len > 0) {
        err[0] = '\0';
    }
    return 0;
}

void app_stop(App *app)
{
    if (app == NULL) {
        return;
    }
    if (!app->acquiring) {
        close_log(app);
        return;
    }
    halt(app, "Stopped", app->kind == APP_SOURCE_TMD56 ? APP_LEVEL_WARNING : APP_LEVEL_IDLE);
}

int app_poll(App *app)
{
    int produced = 0;
    bool track_spikes;
    bool ignore_spikes;
    bool log_problem = false;

    if (app == NULL || !app->acquiring || app->active == NULL) {
        return 0;
    }

    track_spikes = app->kind != APP_SOURCE_TMD56;
    ignore_spikes = app->spike_filter_enabled && track_spikes;

    while (produced < 500) {
        TemperatureMeasurement sample;
        int rc = measurement_source_get(app->active, &sample);
        if (rc == 0) {
            break;
        }
        if (rc < 0) {
            halt(app, measurement_source_status(app->active), APP_LEVEL_ERROR);
            return produced;
        }

        if (track_spikes) {
            int unmark = spike_tracker_observe(&app->spikes, &sample, app->spike_threshold);
            if (history_buffer_push(&app->history, &sample) != 0) {
                halt(app, "Out of memory while storing samples", APP_LEVEL_ERROR);
                return produced;
            }
            if (unmark > 0) {
                clear_tail_marks(app, unmark);
                recompute_stats(app);
            } else {
                stats_accumulate(&app->stats, &sample, ignore_spikes);
                remember_latest(app, &sample);
            }
        } else {
            sample.suspicious = false;
            if (history_buffer_push(&app->history, &sample) != 0) {
                halt(app, "Out of memory while storing samples", APP_LEVEL_ERROR);
                return produced;
            }
            stats_accumulate(&app->stats, &sample, false);
            remember_latest(app, &sample);
        }

        if (logger_is_open(app->logger) &&
            logger_write(app->logger, &sample) != 0) {
            fprintf(stderr, "tmd56: log write failed; recording stopped\n");
            logger_close(app->logger);
            app->log_fault = true;
            log_problem = true;
        }
        produced++;
        if (measurement_source_finished(app->active)) {
            break;
        }
    }

    if (!app->acquiring) {
        return produced;
    }
    if (measurement_source_finished(app->active)) {
        halt(app, "Replay finished", APP_LEVEL_IDLE);
        return produced;
    }
    if (log_problem || app->log_fault) {
        set_status(app, "Logging stopped because a write failed. Acquisition continues.",
                   APP_LEVEL_ERROR);
        return produced;
    }
    /* Leave a fault on the status line until stop, source change, or a new log. */
    if (app->level == APP_LEVEL_ERROR) {
        return produced;
    }
    set_status(app, measurement_source_status(app->active),
               app->kind == APP_SOURCE_TMD56 ? APP_LEVEL_WARNING : APP_LEVEL_RUNNING);
    return produced;
}

void app_replay_pause(App *app, bool paused)
{
    if (app == NULL || app->kind != APP_SOURCE_REPLAY) {
        return;
    }
    replay_source_set_paused(app->replay, paused);
    if (app->acquiring) {
        set_status(app, measurement_source_status(replay_source_base(app->replay)),
                   APP_LEVEL_RUNNING);
    }
}

bool app_replay_paused(const App *app)
{
    return app != NULL && replay_source_is_paused(app->replay);
}

void app_replay_restart(App *app)
{
    bool play;
    MeasurementSource *source;
    if (app == NULL || app->kind != APP_SOURCE_REPLAY) {
        return;
    }
    if (replay_source_sample_count(app->replay) == 0u) {
        return;
    }
    play = app->acquiring || replay_source_is_finished(app->replay);
    close_log(app);
    source = replay_source_base(app->replay);
    replay_source_restart(app->replay);
    history_buffer_clear(&app->history);
    stats_reset(&app->stats);
    spike_tracker_reset(&app->spikes);
    app->have_latest = false;
    measurement_clear(&app->latest);
    app->log_fault = false;
    if (!play) {
        idle_status_for_source(app);
        return;
    }
    if (measurement_source_start(source) != 0) {
        app->acquiring = false;
        set_status(app, measurement_source_status(source), APP_LEVEL_ERROR);
        return;
    }
    app->acquiring = true;
    set_status(app, measurement_source_status(source), APP_LEVEL_RUNNING);
}

int app_start_logging(App *app, const char *session_name, char *err, size_t err_len)
{
    int rc;
    if (app == NULL) {
        return -1;
    }
    if (app->kind == APP_SOURCE_REPLAY) {
        set_status(app, "Replay does not create a second recording", APP_LEVEL_ERROR);
        fprintf(stderr, "tmd56: logging refused during replay\n");
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "%s", app->status);
        }
        return -1;
    }
    if (!app->acquiring) {
        set_status(app, "Start acquisition before logging", APP_LEVEL_ERROR);
        fprintf(stderr, "tmd56: logging refused because acquisition is stopped\n");
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "%s", app->status);
        }
        return -1;
    }
    if (user_logs_ensure(err, err_len) != 0) {
        const char *text = (err != NULL && err_len > 0 && err[0] != '\0')
                               ? err
                               : "Could not create the logs directory";
        set_status(app, text, APP_LEVEL_ERROR);
        return -1;
    }
    rc = logger_open_session(app->logger, user_logs_dir(), session_name, err, err_len);
    if (rc != 0) {
        const char *text = (err != NULL && err_len > 0 && err[0] != '\0')
                               ? err
                               : "Could not start logging";
        set_status(app, text, APP_LEVEL_ERROR);
        return rc;
    }
    app->log_fault = false;
    set_status(app, measurement_source_status(app->active),
               app->kind == APP_SOURCE_TMD56 ? APP_LEVEL_WARNING : APP_LEVEL_RUNNING);
    return 0;
}

void app_stop_logging(App *app)
{
    if (app == NULL) {
        return;
    }
    logger_close(app->logger);
}

void app_set_spike_filter(App *app, bool enabled)
{
    if (app == NULL) {
        return;
    }
    app->spike_filter_enabled = enabled;
    recompute_stats(app);
}

void app_set_spike_threshold(App *app, double threshold_c)
{
    if (app == NULL) {
        return;
    }
    if (!(threshold_c > 0.0)) {
        threshold_c = TMD_DEFAULT_SPIKE_THRESHOLD_C;
    }
    app->spike_threshold = threshold_c;
    if (app->kind == APP_SOURCE_TMD56) {
        return;
    }
    history_buffer_remark_spikes(&app->history, threshold_c, &app->spikes);
    recompute_stats(app);
}

bool app_is_acquiring(const App *app)
{
    return app != NULL && app->acquiring;
}

bool app_is_logging(const App *app)
{
    return app != NULL && logger_is_open(app->logger);
}

AppStatusLevel app_status_level(const App *app)
{
    return app == NULL ? APP_LEVEL_IDLE : app->level;
}

const char *app_status(const App *app)
{
    return app == NULL ? "" : app->status;
}

const char *app_log_path(const App *app)
{
    return app == NULL ? "" : logger_path(app->logger);
}

const HistoryBuffer *app_history(const App *app)
{
    return app == NULL ? NULL : &app->history;
}

const MeasurementStats *app_stats(const App *app)
{
    return app == NULL ? NULL : &app->stats;
}

bool app_has_latest(const App *app)
{
    return app != NULL && app->have_latest;
}

const TemperatureMeasurement *app_latest(const App *app)
{
    if (app == NULL || !app->have_latest) {
        return NULL;
    }
    return &app->latest;
}

bool app_spike_filter_enabled(const App *app)
{
    return app != NULL && app->spike_filter_enabled;
}

size_t app_replay_sample_count(const App *app)
{
    return app == NULL ? 0u : replay_source_sample_count(app->replay);
}

const char *app_replay_path(const App *app)
{
    return app == NULL ? "" : replay_source_path(app->replay);
}

const char *app_replay_summary(const App *app)
{
    return app == NULL ? "" : replay_source_summary(app->replay);
}

bool app_replay_finished(const App *app)
{
    return app != NULL && app->kind == APP_SOURCE_REPLAY &&
           replay_source_is_finished(app->replay);
}
