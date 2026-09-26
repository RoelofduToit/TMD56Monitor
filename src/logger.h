#ifndef TMD56_LOGGER_H
#define TMD56_LOGGER_H

#include "measurement.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct Logger Logger;

Logger *logger_create(void);
void logger_destroy(Logger *logger);

/*
 * Opens <directory>/<session>_<local timestamp>.csv and writes the header.
 * The caller supplies a writable directory. NULL still means "logs".
 * Invalid samples are written with empty temperature fields and valid=0.
 * Suspicious marks are ignored: the logged numbers are the raw sample.
 */
int logger_open_session(Logger *logger, const char *directory,
                        const char *session_name, char *err, size_t err_len);
void logger_close(Logger *logger);
int logger_write(Logger *logger, const TemperatureMeasurement *sample);

bool logger_is_open(const Logger *logger);
const char *logger_path(const Logger *logger);

#endif
