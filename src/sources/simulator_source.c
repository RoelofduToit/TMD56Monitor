#define _POSIX_C_SOURCE 200809L

#include "sources/simulator_source.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default sample period. The model advances only while it is polled. */
static const double SIM_PERIOD_DEFAULT_S = 0.5;
static const double SIM_PI = 3.14159265358979323846;

struct SimulatorSource {
    MeasurementSource base;
    SimulatorScenario scenario;
    double t1;
    double t2;
    double elapsed;
    double next_due;
    double period_s;
    double origin_mono;
    bool have_emitted;
    uint32_t rng;
    unsigned int sample_index;
};

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    if (x == 0u) {
        x = 0xA3C59AC3u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0u) {
        x = 0xA3C59AC3u;
    }
    *state = x;
    return x;
}

static double sim_uniform(uint32_t *state)
{
    return ((double)xorshift32(state) + 0.5) / 4294967296.0;
}

static double sim_gaussian(uint32_t *state)
{
    double u1 = sim_uniform(state);
    double u2 = sim_uniform(state);
    if (u1 < 1e-12) {
        u1 = 1e-12;
    }
    return sqrt(-2.0 * log(u1)) * cos(2.0 * SIM_PI * u2);
}

const char *simulator_scenario_name(SimulatorScenario scenario)
{
    switch (scenario) {
    case SIM_STABLE:
        return "Stable";
    case SIM_HEATING:
        return "Heating";
    case SIM_COOLING:
        return "Cooling";
    case SIM_GRADIENT:
        return "Thermal gradient";
    case SIM_NOISE:
        return "Noise";
    case SIM_SPIKE:
        return "Occasional spike";
    default:
        return "Stable";
    }
}

static void sim_reset_model(SimulatorSource *source)
{
    source->t1 = 25.0;
    source->t2 = 25.0;
    source->elapsed = 0.0;
    source->have_emitted = false;
    source->sample_index = 0;
    source->rng = (uint32_t)(tmd_monotonic_seconds() * 1000000.0);
    if (source->rng == 0u) {
        source->rng = 0x6D2B79F5u;
    }
}

static void sim_evolve(SimulatorSource *source, double dt)
{
    double elapsed = source->elapsed + dt;
    double n1 = sim_gaussian(&source->rng);
    double n2 = sim_gaussian(&source->rng);

    if (source->scenario == SIM_GRADIENT) {
        double set1 = 25.0 + 20.0 * (1.0 - exp(-elapsed / 150.0));
        double set2 = 25.0 - 10.0 * (1.0 - exp(-elapsed / 180.0));
        source->t1 += (set1 - source->t1) * (1.0 - exp(-dt / 30.0)) + 0.01 * n1;
        source->t2 += (set2 - source->t2) * (1.0 - exp(-dt / 40.0)) + 0.01 * n2;
    } else if (source->scenario == SIM_HEATING || source->scenario == SIM_COOLING) {
        double set1 = (source->scenario == SIM_HEATING)
                          ? 25.0 + 60.0 * (1.0 - exp(-elapsed / 120.0))
                          : 25.0 - 28.0 * (1.0 - exp(-elapsed / 100.0));
        source->t1 += (set1 - source->t1) * (1.0 - exp(-dt / 20.0)) + 0.01 * n1;
        /* T2 is not heated directly; it follows T1 more slowly. */
        source->t2 += (source->t1 - source->t2) * (1.0 - exp(-dt / 55.0)) + 0.01 * n2;
    } else {
        double wander = (source->scenario == SIM_NOISE) ? 0.08 : 0.012;
        double set1 = 25.0 + 0.15 * sin(elapsed / 45.0);
        double set2 = 25.08 + 0.12 * sin(elapsed / 70.0 + 0.6);
        source->t1 += (set1 - source->t1) * (1.0 - exp(-dt / 12.0)) + wander * n1;
        source->t2 += (set2 - source->t2) * (1.0 - exp(-dt / 15.0)) + wander * n2;
    }
    source->elapsed = elapsed;
}

static void sim_publish(SimulatorSource *source, const char *verb)
{
    snprintf(source->base.status, sizeof source->base.status,
             "Simulator %s — %s — %.1f s", verb,
             simulator_scenario_name(source->scenario), source->elapsed);
}

static int sim_open(MeasurementSource *self, const char *target)
{
    SimulatorSource *source = (SimulatorSource *)self;
    (void)target;
    sim_reset_model(source);
    self->is_open = true;
    sim_publish(source, "ready");
    return 0;
}

static void sim_close(MeasurementSource *self)
{
    self->is_open = false;
    self->running = false;
}

static int sim_start(MeasurementSource *self)
{
    SimulatorSource *source = (SimulatorSource *)self;
    if (!self->is_open) {
        if (sim_open(self, NULL) != 0) {
            return -1;
        }
    }
    sim_reset_model(source);
    source->origin_mono = tmd_monotonic_seconds();
    source->next_due = source->origin_mono;
    self->running = true;
    sim_publish(source, "running");
    return 0;
}

static void sim_stop(MeasurementSource *self)
{
    self->running = false;
    snprintf(self->status, sizeof self->status, "Simulator stopped — %s",
             simulator_scenario_name(((SimulatorSource *)self)->scenario));
}

static int sim_get(MeasurementSource *self, TemperatureMeasurement *out)
{
    SimulatorSource *source = (SimulatorSource *)self;
    double now;
    double due;
    double meas_sigma;
    double t1;
    double t2;

    if (!self->running || out == NULL) {
        return 0;
    }
    now = tmd_monotonic_seconds();
    if (source->period_s < 0.1) {
        source->period_s = SIM_PERIOD_DEFAULT_S;
    }
    /*
     * Samples are due on a monotonic schedule. Elapsed time is that
     * scheduled instant, not the moment the UI happens to poll.
     */
    due = source->next_due;
    if (now + 1e-4 < due) {
        return 0;
    }
    if (!source->have_emitted) {
        /* The opening sample is the start of the run, even if the UI polls late. */
        source->have_emitted = true;
        source->elapsed = 0.0;
    } else {
        double published;
        double step;
        if (now - due > 2.0) {
            due = now;
        }
        published = due - source->origin_mono;
        if (published < source->elapsed) {
            published = source->elapsed;
        }
        step = published - source->elapsed;
        if (step > 0.0) {
            sim_evolve(source, step);
        }
        source->elapsed = published;
    }
    source->next_due = due + source->period_s;

    source->sample_index++;
    meas_sigma = (source->scenario == SIM_NOISE) ? 0.08 : 0.015;
    t1 = source->t1 + meas_sigma * sim_gaussian(&source->rng);
    t2 = source->t2 + meas_sigma * sim_gaussian(&source->rng);

    /*
     * Spikes are applied only to the emitted sample. The thermal state
     * stays continuous, so the next reading returns to the real curve.
     */
    if (source->scenario == SIM_SPIKE &&
        source->sample_index > 1u &&
        (source->sample_index % 40u) == 0u) {
        if ((source->sample_index % 80u) == 0u) {
            t1 += 45.0;
        } else {
            t2 -= 38.0;
        }
    }

    measurement_set(out, t1, t2, source->elapsed, true);
    sim_publish(source, "running");
    return 1;
}

static const char *sim_status(const MeasurementSource *self)
{
    return self->status;
}

static bool sim_finished(const MeasurementSource *self)
{
    (void)self;
    return false;
}

static const MeasurementSourceVTable SIM_VTABLE = {
    .open = sim_open,
    .close = sim_close,
    .start = sim_start,
    .stop = sim_stop,
    .get_measurement = sim_get,
    .status = sim_status,
    .finished = sim_finished,
};

SimulatorSource *simulator_source_create(void)
{
    SimulatorSource *source = calloc(1, sizeof(*source));
    if (source == NULL) {
        return NULL;
    }
    source->base.vtable = &SIM_VTABLE;
    source->scenario = SIM_STABLE;
    source->period_s = SIM_PERIOD_DEFAULT_S;
    sim_reset_model(source);
    snprintf(source->base.status, sizeof source->base.status,
             "Simulator ready — Stable");
    return source;
}

void simulator_source_destroy(SimulatorSource *source)
{
    free(source);
}

MeasurementSource *simulator_source_base(SimulatorSource *source)
{
    return source == NULL ? NULL : &source->base;
}

void simulator_source_set_scenario(SimulatorSource *source, SimulatorScenario scenario)
{
    if (source == NULL) {
        return;
    }
    if ((int)scenario < 0 || scenario >= SIM_SCENARIO_COUNT) {
        scenario = SIM_STABLE;
    }
    source->scenario = scenario;
    if (!source->base.running) {
        sim_publish(source, source->base.is_open ? "ready" : "ready");
    }
}

SimulatorScenario simulator_source_scenario(const SimulatorSource *source)
{
    return source == NULL ? SIM_STABLE : source->scenario;
}

void simulator_source_set_period(SimulatorSource *source, double seconds)
{
    if (source == NULL) {
        return;
    }
    if (seconds < 0.1 || seconds > 5.0) {
        return;
    }
    source->period_s = seconds;
    if (source->base.running) {
        source->next_due = tmd_monotonic_seconds() + source->period_s;
    }
}

double simulator_source_period(const SimulatorSource *source)
{
    return source == NULL ? SIM_PERIOD_DEFAULT_S : source->period_s;
}
