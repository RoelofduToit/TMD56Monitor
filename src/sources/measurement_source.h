#ifndef TMD56_MEASUREMENT_SOURCE_H
#define TMD56_MEASUREMENT_SOURCE_H

#include "measurement.h"

#include <stdbool.h>

/*
 * Acquisition interface used by the rest of the program.
 * Simulator, file replay, and the physical TMD-56 all look like this.
 *
 * get_measurement:
 *   1  a new sample was written
 *   0  nothing ready yet
 *  -1  the source failed
 */
typedef struct MeasurementSource MeasurementSource;

typedef struct {
    int (*open)(MeasurementSource *self, const char *target);
    void (*close)(MeasurementSource *self);
    int (*start)(MeasurementSource *self);
    void (*stop)(MeasurementSource *self);
    int (*get_measurement)(MeasurementSource *self, TemperatureMeasurement *out);
    const char *(*status)(const MeasurementSource *self);
    bool (*finished)(const MeasurementSource *self);
} MeasurementSourceVTable;

struct MeasurementSource {
    const MeasurementSourceVTable *vtable;
    bool is_open;
    bool running;
    char status[384];
};

static inline int measurement_source_open(MeasurementSource *self, const char *target)
{
    if (self == NULL || self->vtable == NULL || self->vtable->open == NULL) {
        return -1;
    }
    return self->vtable->open(self, target);
}

static inline void measurement_source_close(MeasurementSource *self)
{
    if (self != NULL && self->vtable != NULL && self->vtable->close != NULL) {
        self->vtable->close(self);
    }
}

static inline int measurement_source_start(MeasurementSource *self)
{
    if (self == NULL || self->vtable == NULL || self->vtable->start == NULL) {
        return -1;
    }
    return self->vtable->start(self);
}

static inline void measurement_source_stop(MeasurementSource *self)
{
    if (self != NULL && self->vtable != NULL && self->vtable->stop != NULL) {
        self->vtable->stop(self);
    }
}

static inline int measurement_source_get(MeasurementSource *self,
                                         TemperatureMeasurement *out)
{
    if (self == NULL || self->vtable == NULL || self->vtable->get_measurement == NULL) {
        return -1;
    }
    return self->vtable->get_measurement(self, out);
}

static inline const char *measurement_source_status(const MeasurementSource *self)
{
    if (self == NULL || self->vtable == NULL || self->vtable->status == NULL) {
        return "";
    }
    return self->vtable->status(self);
}

static inline bool measurement_source_finished(const MeasurementSource *self)
{
    if (self == NULL || self->vtable == NULL || self->vtable->finished == NULL) {
        return false;
    }
    return self->vtable->finished(self);
}

#endif
