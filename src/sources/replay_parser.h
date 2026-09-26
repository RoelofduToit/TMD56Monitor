#ifndef TMD56_REPLAY_PARSER_H
#define TMD56_REPLAY_PARSER_H

#include <stdbool.h>
#include <stddef.h>

/*
 * One row from a TMD-56 export or from a CSV written by this program.
 * Invalid channels are NAN, never a fabricated 0.
 * Elapsed time is seconds from the first kept sample.
 */
typedef struct {
    double elapsed_seconds;
    double t1;
    double t2;
    bool valid;
} ReplaySample;

typedef struct {
    size_t loaded;
    size_t skipped;
    size_t duplicate_timestamps;
    size_t backward_timestamps;
    char summary[256];
} ReplayParseReport;

/*
 * Reads a text export. Metadata before the samples is ignored.
 * report may be NULL. On success summary is a single line, for example
 * "Loaded 1,243 samples. Skipped 3 malformed rows."
 * Returns 0 on success and -1 if the file cannot be used at all.
 */
int replay_parse_path(const char *path, ReplaySample **out_samples, size_t *out_count,
                      ReplayParseReport *report, char *error, size_t error_len);
void replay_samples_free(ReplaySample *samples);

#endif
