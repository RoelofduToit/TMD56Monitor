#ifndef TMD56_REPLAY_SOURCE_H
#define TMD56_REPLAY_SOURCE_H

#include "sources/measurement_source.h"
#include "sources/replay_parser.h"

typedef struct ReplaySource ReplaySource;

ReplaySource *replay_source_create(void);
void replay_source_destroy(ReplaySource *source);
MeasurementSource *replay_source_base(ReplaySource *source);

void replay_source_set_speed(ReplaySource *source, double speed);
double replay_source_speed(const ReplaySource *source);
void replay_source_set_paused(ReplaySource *source, bool paused);
bool replay_source_is_paused(const ReplaySource *source);
void replay_source_restart(ReplaySource *source);
bool replay_source_is_finished(const ReplaySource *source);
size_t replay_source_sample_count(const ReplaySource *source);
const char *replay_source_path(const ReplaySource *source);
const char *replay_source_summary(const ReplaySource *source);

#endif
