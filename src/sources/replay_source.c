#define _POSIX_C_SOURCE 200809L

#include "sources/replay_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ReplaySource {
    MeasurementSource base;
    ReplaySample *samples;
    size_t count;
    size_t next_index;
    double speed;
    double playback_base;
    double origin;
    bool paused;
    bool finished;
    char path[768];
    char summary[256];
};

static double replay_file_time(const ReplaySource *source)
{
    if (!source->base.running || source->paused) {
        return source->playback_base;
    }
    return source->playback_base +
           (tmd_monotonic_seconds() - source->origin) * source->speed;
}

static void replay_capture(ReplaySource *source)
{
    source->playback_base = replay_file_time(source);
    source->origin = tmd_monotonic_seconds();
}

static const char *file_name(const ReplaySource *source)
{
    const char *slash = strrchr(source->path, '/');
    if (slash != NULL && slash[1] != '\0') {
        return slash + 1;
    }
    return source->path;
}

static void replay_describe(ReplaySource *source)
{
    const char *name = file_name(source);
    if (source->finished) {
        snprintf(source->base.status, sizeof source->base.status,
                 "Replay complete — %zu samples — %.80s", source->count, name);
    } else if (!source->base.is_open) {
        snprintf(source->base.status, sizeof source->base.status, "No replay file loaded");
    } else if (source->paused) {
        snprintf(source->base.status, sizeof source->base.status, "Replay paused — %.80s", name);
    } else if (source->base.running) {
        snprintf(source->base.status, sizeof source->base.status,
                 "Replaying — %.80s", name);
    } else if (source->summary[0] != '\0') {
        snprintf(source->base.status, sizeof source->base.status, "%s", source->summary);
    } else {
        snprintf(source->base.status, sizeof source->base.status,
                 "Loaded %zu samples — %.80s", source->count, name);
    }
}

static int replay_open(MeasurementSource *self, const char *target)
{
    ReplaySource *source = (ReplaySource *)self;
    ReplaySample *parsed = NULL;
    size_t count = 0;
    ReplayParseReport report;
    char error[256];

    error[0] = '\0';
    memset(&report, 0, sizeof report);
    if (replay_parse_path(target, &parsed, &count, &report, error, sizeof error) != 0) {
        snprintf(self->status, sizeof self->status, "%s",
                 error[0] != '\0' ? error : "Could not read the replay file");
        return -1;
    }
    free(source->samples);
    source->samples = parsed;
    source->count = count;
    source->next_index = 0;
    source->playback_base = 0.0;
    source->paused = false;
    source->finished = false;
    self->running = false;
    self->is_open = true;
    snprintf(source->path, sizeof source->path, "%s", target != NULL ? target : "");
    snprintf(source->summary, sizeof source->summary, "%s", report.summary);
    replay_describe(source);
    return 0;
}

static void replay_close(MeasurementSource *self)
{
    ReplaySource *source = (ReplaySource *)self;
    free(source->samples);
    source->samples = NULL;
    source->count = 0;
    source->next_index = 0;
    source->paused = false;
    source->finished = false;
    source->path[0] = '\0';
    source->summary[0] = '\0';
    self->is_open = false;
    self->running = false;
    snprintf(self->status, sizeof self->status, "No replay file loaded");
}

static int replay_start(MeasurementSource *self)
{
    ReplaySource *source = (ReplaySource *)self;
    if (!self->is_open || source->samples == NULL || source->count == 0u) {
        snprintf(self->status, sizeof self->status, "Choose a replay file before starting");
        return -1;
    }
    source->next_index = 0;
    source->playback_base = 0.0;
    source->origin = tmd_monotonic_seconds();
    source->paused = false;
    source->finished = false;
    self->running = true;
    replay_describe(source);
    return 0;
}

static void replay_stop(MeasurementSource *self)
{
    ReplaySource *source = (ReplaySource *)self;
    if (self->running && !source->paused) {
        replay_capture(source);
    }
    self->running = false;
    source->paused = false;
    if (!source->finished) {
        replay_describe(source);
    }
}

static int replay_get(MeasurementSource *self, TemperatureMeasurement *out)
{
    ReplaySource *source = (ReplaySource *)self;
    double file_time;
    ReplaySample *sample;

    if (!self->running || source->paused || source->finished || out == NULL) {
        return 0;
    }
    if (source->next_index >= source->count) {
        source->finished = true;
        self->running = false;
        replay_describe(source);
        return 0;
    }

    file_time = replay_file_time(source);
    sample = &source->samples[source->next_index];
    /* Real time follows the original spacing, scaled by the playback speed. */
    if (sample->elapsed_seconds > file_time + 1e-6) {
        return 0;
    }

    measurement_set(out, sample->t1, sample->t2, sample->elapsed_seconds, sample->valid);
    source->next_index++;
    if (source->next_index >= source->count) {
        source->finished = true;
    }
    replay_describe(source);
    return 1;
}

static const char *replay_status(const MeasurementSource *self)
{
    return self->status;
}

static bool replay_finished_cb(const MeasurementSource *self)
{
    return ((const ReplaySource *)self)->finished;
}

static const MeasurementSourceVTable REPLAY_VTABLE = {
    .open = replay_open,
    .close = replay_close,
    .start = replay_start,
    .stop = replay_stop,
    .get_measurement = replay_get,
    .status = replay_status,
    .finished = replay_finished_cb,
};

ReplaySource *replay_source_create(void)
{
    ReplaySource *source = calloc(1, sizeof(*source));
    if (source == NULL) {
        return NULL;
    }
    source->base.vtable = &REPLAY_VTABLE;
    source->speed = 1.0;
    snprintf(source->base.status, sizeof source->base.status, "No replay file loaded");
    return source;
}

void replay_source_destroy(ReplaySource *source)
{
    if (source == NULL) {
        return;
    }
    free(source->samples);
    free(source);
}

MeasurementSource *replay_source_base(ReplaySource *source)
{
    return source == NULL ? NULL : &source->base;
}

void replay_source_set_speed(ReplaySource *source, double speed)
{
    if (source == NULL) {
        return;
    }
    if (!(speed > 0.0)) {
        speed = 1.0;
    }
    if (source->base.running && !source->paused) {
        replay_capture(source);
    }
    source->speed = speed;
    source->origin = tmd_monotonic_seconds();
    replay_describe(source);
}

double replay_source_speed(const ReplaySource *source)
{
    return source == NULL ? 1.0 : source->speed;
}

void replay_source_set_paused(ReplaySource *source, bool paused)
{
    if (source == NULL || !source->base.running || source->finished) {
        return;
    }
    if (paused && !source->paused) {
        replay_capture(source);
        source->paused = true;
    } else if (!paused && source->paused) {
        source->origin = tmd_monotonic_seconds();
        source->paused = false;
    }
    replay_describe(source);
}

bool replay_source_is_paused(const ReplaySource *source)
{
    return source != NULL && source->paused;
}

void replay_source_restart(ReplaySource *source)
{
    if (source == NULL) {
        return;
    }
    source->next_index = 0;
    source->playback_base = 0.0;
    source->origin = tmd_monotonic_seconds();
    source->paused = false;
    source->finished = false;
    replay_describe(source);
}

bool replay_source_is_finished(const ReplaySource *source)
{
    return source != NULL && source->finished;
}

size_t replay_source_sample_count(const ReplaySource *source)
{
    return source == NULL ? 0u : source->count;
}

const char *replay_source_path(const ReplaySource *source)
{
    return source == NULL ? "" : source->path;
}

const char *replay_source_summary(const ReplaySource *source)
{
    if (source == NULL || source->summary[0] == '\0') {
        return "";
    }
    return source->summary;
}
