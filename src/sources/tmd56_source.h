#ifndef TMD56_TMD56_SOURCE_H
#define TMD56_TMD56_SOURCE_H

#include "sources/measurement_source.h"

/*
 * Physical Amprobe TMD-56 source.
 *
 * EXPERIMENTAL / UNVERIFIED.
 * The serial settings and frame notes below come from other people's
 * reports. They have not been checked against the instrument this
 * program will be used with. This module never invents temperatures.
 * Spike filtering is not applied here; the application also skips it
 * for this source so a future raw-protocol debug path stays untouched.
 */

typedef struct Tmd56Source Tmd56Source;

Tmd56Source *tmd56_source_create(void);
void tmd56_source_destroy(Tmd56Source *source);
MeasurementSource *tmd56_source_base(Tmd56Source *source);

/* Reported query string. It is not transmitted. */
const char *tmd56_reported_query_unverified(void);

#endif
