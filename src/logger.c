#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include "platform/compat.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct Logger {
    FILE *file;
    char path[768];
    int rows_since_sync;
};

Logger *logger_create(void)
{
    Logger *logger = calloc(1, sizeof(*logger));
    return logger;
}

void logger_destroy(Logger *logger)
{
    if (logger == NULL) {
        return;
    }
    logger_close(logger);
    free(logger);
}

static void set_err(char *err, size_t err_len, const char *text)
{
    if (err != NULL && err_len > 0) {
        snprintf(err, err_len, "%s", text);
    }
    if (text != NULL && text[0] != '\0') {
        fprintf(stderr, "tmd56: %s\n", text);
    }
}

static void sanitize_session(const char *in, char *out, size_t out_len)
{
    size_t j = 0;
    bool skipped = true;
    size_t i;

    if (out_len == 0) {
        return;
    }
    for (i = 0; in[i] != '\0' && j + 1u < out_len && j < 80u; i++) {
        unsigned char c = (unsigned char)in[i];
        char replacement;
        if (isalnum(c)) {
            replacement = (char)c;
            skipped = false;
        } else if (c == '-' || c == '_') {
            if (skipped) {
                continue;
            }
            replacement = (char)c;
        } else if (c == ' ') {
            if (skipped) {
                continue;
            }
            replacement = ' ';
            skipped = true;
            out[j++] = replacement;
            continue;
        } else {
            if (skipped) {
                continue;
            }
            replacement = '_';
            skipped = true;
            out[j++] = replacement;
            continue;
        }
        skipped = false;
        out[j++] = replacement;
    }
    while (j > 0 && (out[j - 1u] == ' ' || out[j - 1u] == '_' || out[j - 1u] == '-')) {
        j--;
    }
    out[j] = '\0';
}

static int ensure_dir(const char *directory, char *err, size_t err_len)
{
    if (tmd_mkdir(directory) == 0) {
        return 0;
    }
    fprintf(stderr, "tmd56: mkdir %s failed: %s\n", directory, strerror(errno));
    if (err != NULL && err_len > 0) {
        snprintf(err, err_len, "%s", "Could not create the logs directory");
    }
    return -1;
}

int logger_open_session(Logger *logger, const char *directory,
                        const char *session_name, char *err, size_t err_len)
{
    char clean[96];
    char stamp[32];
    const char *dir = (directory != NULL && directory[0] != '\0') ? directory : "logs";
    struct tm tm_buf;
    time_t now;
    int suffix;
    int fd = -1;

    if (logger == NULL) {
        set_err(err, err_len, "Logger is not available");
        return -1;
    }
    if (logger->file != NULL) {
        set_err(err, err_len, "Logging is already running");
        return -1;
    }
    if (session_name == NULL) {
        set_err(err, err_len, "Enter a session name before logging");
        return -1;
    }
    sanitize_session(session_name, clean, sizeof clean);
    if (clean[0] == '\0') {
        set_err(err, err_len, "Enter a session name before logging");
        return -1;
    }
    if (ensure_dir(dir, err, err_len) != 0) {
        return -1;
    }

    now = time(NULL);
    if (tmd_localtime(&now, &tm_buf) != 0) {
        set_err(err, err_len, "Could not read the local time");
        return -1;
    }
    snprintf(stamp, sizeof stamp, "%04d%02d%02d_%02d%02d%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    for (suffix = 0; suffix < 100; suffix++) {
        if (suffix == 0) {
            snprintf(logger->path, sizeof logger->path, "%s/%s_%s.csv", dir, clean, stamp);
        } else {
            snprintf(logger->path, sizeof logger->path, "%s/%s_%s_%d.csv",
                     dir, clean, stamp, suffix);
        }
        fd = open(logger->path, O_CREAT | O_EXCL | O_WRONLY | O_BINARY, 0644);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            fprintf(stderr, "tmd56: cannot create %s: %s\n", logger->path, strerror(errno));
            logger->path[0] = '\0';
            set_err(err, err_len, "Could not create the log file");
            return -1;
        }
    }
    if (fd < 0) {
        logger->path[0] = '\0';
        set_err(err, err_len, "Could not find a free log filename");
        return -1;
    }

    logger->file = fdopen(fd, "w");
    if (logger->file == NULL) {
        close(fd);
        unlink(logger->path);
        logger->path[0] = '\0';
        set_err(err, err_len, "Could not create the log file");
        return -1;
    }
    if (fprintf(logger->file, "timestamp,elapsed_s,t1_c,t2_c,delta_t_c,valid\n") < 0) {
        fclose(logger->file);
        logger->file = NULL;
        unlink(logger->path);
        logger->path[0] = '\0';
        set_err(err, err_len, "Could not write the log header");
        return -1;
    }
    fflush(logger->file);
    logger->rows_since_sync = 0;
    return 0;
}

void logger_close(Logger *logger)
{
    if (logger == NULL || logger->file == NULL) {
        return;
    }
    fflush(logger->file);
    tmd_fsync(fileno(logger->file));
    fclose(logger->file);
    logger->file = NULL;
    logger->rows_since_sync = 0;
}

/* Always use a decimal point, independent of the process locale. */
static void write_fixed(FILE *file, double value)
{
    int negative;
    double magnitude;
    long long scaled;
    long long whole;
    long long fraction;

    if (!isfinite(value) || fabs(value) < 0.0005) {
        value = 0.0;
    }
    negative = value < 0.0;
    magnitude = negative ? -value : value;
    scaled = (long long)(magnitude * 1000.0 + 0.5);
    whole = scaled / 1000;
    fraction = scaled % 1000;
    if (negative && scaled != 0) {
        fputc('-', file);
    }
    fprintf(file, "%lld.%03lld", whole, fraction);
}

static void write_optional(FILE *file, bool present, double value)
{
    if (present && isfinite(value)) {
        write_fixed(file, value);
    }
}

int logger_write(Logger *logger, const TemperatureMeasurement *sample)
{
    char stamp[40];

    if (logger == NULL || logger->file == NULL || sample == NULL) {
        return -1;
    }
    tmd_format_timestamp(stamp, sizeof stamp);
    if (fprintf(logger->file, "%s,", stamp) < 0) {
        return -1;
    }
    write_fixed(logger->file, sample->elapsed_seconds);
    if (fputc(',', logger->file) == EOF) {
        return -1;
    }
    write_optional(logger->file, isfinite(sample->t1), sample->t1);
    fputc(',', logger->file);
    write_optional(logger->file, isfinite(sample->t2), sample->t2);
    fputc(',', logger->file);
    write_optional(logger->file, isfinite(sample->delta_t), sample->delta_t);
    if (fprintf(logger->file, ",%d\n", sample->valid ? 1 : 0) < 0) {
        return -1;
    }

    /* Flush every row so a crash keeps the samples already accepted. */
    if (fflush(logger->file) != 0) {
        return -1;
    }
    logger->rows_since_sync++;
    if (logger->rows_since_sync >= 20) {
        if (tmd_fsync(fileno(logger->file)) != 0) {
            return -1;
        }
        logger->rows_since_sync = 0;
    }
    return 0;
}

bool logger_is_open(const Logger *logger)
{
    return logger != NULL && logger->file != NULL;
}

const char *logger_path(const Logger *logger)
{
    if (logger == NULL) {
        return "";
    }
    return logger->path;
}
