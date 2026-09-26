#define _POSIX_C_SOURCE 200809L

#include "logger.h"
#include "measurement.h"
#include "platform/compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect_true(int condition, const char *file, int line, const char *text)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s:%d %s\n", file, line, text);
        failures++;
    }
}

#define EXPECT(condition) expect_true((condition), __FILE__, __LINE__, #condition)

static char *read_all(const char *path)
{
    FILE *file = fopen(path, "r");
    char *buf;
    long length;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    buf = malloc((size_t)length + 1u);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(buf, 1, (size_t)length, file) != (size_t)length) {
        free(buf);
        fclose(file);
        return NULL;
    }
    buf[length] = '\0';
    fclose(file);
    return buf;
}

int main(void)
{
    char dir[512];
    char err[128];
    Logger *logger;
    Logger *again;
    TemperatureMeasurement sample;
    char *text;
    const char *path;
    char saved[768];

    if (tmd_temp_dir(dir, sizeof dir, "tmd56-log") != 0) {
        fprintf(stderr, "FAIL could not create a temporary directory\n");
        return EXIT_FAILURE;
    }

    logger = logger_create();
    EXPECT(logger_open_session(logger, dir, "   ", err, sizeof err) != 0);
    EXPECT(logger_open_session(logger, dir, "bad/name", err, sizeof err) == 0);
    path = logger_path(logger);
    EXPECT(path != NULL && strstr(path, "bad_name_") != NULL);
    EXPECT(strstr(path, "bad/name") == NULL);
    snprintf(saved, sizeof saved, "%s", path);

    measurement_set(&sample, 24.5, 24.6, 1.25, true);
    EXPECT(logger_write(logger, &sample) == 0);
    measurement_clear(&sample);
    sample.elapsed_seconds = 2.5;
    EXPECT(logger_write(logger, &sample) == 0);
    logger_close(logger);

    text = read_all(saved);
    EXPECT(text != NULL);
    if (text != NULL) {
        EXPECT(strncmp(text, "timestamp,elapsed_s,t1_c,t2_c,delta_t_c,valid\n", 46) == 0);
        EXPECT(strstr(text, ",1.250,24.500,24.600,-0.100,1\n") != NULL);
        EXPECT(strstr(text, ",2.500,,,,0\n") != NULL);
        EXPECT(strstr(text, ",2.500,0.000,0.000,0.000,0") == NULL);
        free(text);
    }

    again = logger_create();
    EXPECT(logger_open_session(again, dir, "bad/name", err, sizeof err) == 0);
    EXPECT(strcmp(logger_path(again), saved) != 0);
    text = read_all(saved);
    EXPECT(text != NULL && strstr(text, ",1.250,24.500,24.600,-0.100,1\n") != NULL);
    free(text);
    logger_close(again);
    logger_destroy(logger);
    logger_destroy(again);

    if (failures != 0) {
        fprintf(stderr, "%d logger check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("logger tests passed\n");
    return EXIT_SUCCESS;
}
