#define _POSIX_C_SOURCE 200809L

#include "sources/replay_parser.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double elapsed;
    double t1;
    double t2;
    bool valid;
    bool from_clock;
    double clock_seconds;
} ParsedRow;

static void set_error(char *error, size_t error_len, const char *text)
{
    if (error != NULL && error_len > 0) {
        snprintf(error, error_len, "%s", text != NULL ? text : "");
    }
}

static void format_count(char *buf, size_t len, size_t value)
{
    char digits[32];
    char out[32];
    int count;
    int index;
    int written = 0;

    snprintf(digits, sizeof digits, "%zu", value);
    count = (int)strlen(digits);
    for (index = 0; index < count && written + 1 < (int)sizeof out; index++) {
        int remaining = count - index;
        if (index > 0 && remaining % 3 == 0) {
            out[written++] = ',';
        }
        out[written++] = digits[index];
    }
    out[written] = '\0';
    snprintf(buf, len, "%s", out);
}

static char *trim_field(char *field)
{
    char *end;
    while (*field == ' ' || *field == '\t' || *field == '\r') {
        field++;
    }
    end = field + strlen(field);
    while (end > field && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = '\0';
    }
    return field;
}

static bool parse_number(const char *text, double *out)
{
    char *end = NULL;
    double value;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    value = strtod(text, &end);
    if (end == text) {
        return false;
    }
    while (*end == ' ') {
        end++;
    }
    if (*end != '\0' || !isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

/* HH:MM:SS with an optional fractional second. The whole field must match. */
static bool parse_hms(const char *text, double *seconds_out)
{
    int hour = 0;
    int minute = 0;
    int second = 0;
    int index = 0;
    double fraction = 0.0;

    if (text == NULL || !isdigit((unsigned char)text[0])) {
        return false;
    }
    while (isdigit((unsigned char)text[index])) {
        hour = hour * 10 + (text[index] - '0');
        index++;
        if (hour > 99) {
            return false;
        }
    }
    if (text[index++] != ':') {
        return false;
    }
    if (!isdigit((unsigned char)text[index])) {
        return false;
    }
    while (isdigit((unsigned char)text[index])) {
        minute = minute * 10 + (text[index] - '0');
        index++;
    }
    if (text[index++] != ':') {
        return false;
    }
    if (!isdigit((unsigned char)text[index])) {
        return false;
    }
    while (isdigit((unsigned char)text[index])) {
        second = second * 10 + (text[index] - '0');
        index++;
    }
    if (text[index] == '.') {
        double place = 0.1;
        index++;
        if (!isdigit((unsigned char)text[index])) {
            return false;
        }
        while (isdigit((unsigned char)text[index])) {
            fraction += (double)(text[index] - '0') * place;
            place *= 0.1;
            index++;
        }
    }
    if (text[index] != '\0' || minute > 59 || second > 60) {
        return false;
    }
    *seconds_out = (double)(hour * 3600 + minute * 60 + second) + fraction;
    return true;
}

static size_t split_fields(char *line, char **fields, size_t max_fields)
{
    char delimiter = 0;
    size_t count = 0;
    size_t i;

    if (strchr(line, '\t') != NULL) {
        delimiter = '\t';
    } else if (strchr(line, ',') != NULL) {
        delimiter = ',';
    }

    if (delimiter != 0) {
        char *cursor = line;
        while (count < max_fields) {
            char *cut;
            fields[count++] = cursor;
            cut = strchr(cursor, delimiter);
            if (cut == NULL) {
                break;
            }
            *cut = '\0';
            cursor = cut + 1;
        }
    } else {
        char *save = NULL;
        char *token = strtok_r(line, " \t", &save);
        while (token != NULL && count < max_fields) {
            fields[count++] = token;
            token = strtok_r(NULL, " \t", &save);
        }
    }

    for (i = 0; i < count; i++) {
        fields[i] = trim_field(fields[i]);
    }
    return count;
}

static bool same_token(const char *text, const char *literal)
{
    size_t i;
    if (text == NULL) {
        return false;
    }
    for (i = 0; literal[i] != '\0'; i++) {
        if (tolower((unsigned char)text[i]) != (unsigned char)literal[i]) {
            return false;
        }
    }
    return text[i] == '\0';
}

/* 1 = number, 0 = blank or OL, -1 = some other token. */
static int channel_kind(const char *text, double *out)
{
    if (text == NULL || text[0] == '\0' || same_token(text, "ol")) {
        *out = nan("");
        return 0;
    }
    if (parse_number(text, out)) {
        return 1;
    }
    *out = nan("");
    return -1;
}

typedef enum {
    ROLE_NONE = 0,
    ROLE_TIME,
    ROLE_T1,
    ROLE_T2,
    ROLE_VALID
} ColumnRole;

static ColumnRole column_role(const char *text)
{
    if (same_token(text, "time") || same_token(text, "elapsed") ||
        same_token(text, "elapsed_s") || same_token(text, "elapsed_seconds")) {
        return ROLE_TIME;
    }
    if (same_token(text, "ch01") || same_token(text, "ch1") ||
        same_token(text, "t1") || same_token(text, "t1_c")) {
        return ROLE_T1;
    }
    if (same_token(text, "ch02") || same_token(text, "ch2") ||
        same_token(text, "t2") || same_token(text, "t2_c")) {
        return ROLE_T2;
    }
    if (same_token(text, "valid")) {
        return ROLE_VALID;
    }
    return ROLE_NONE;
}

/* 1 = usable header, -1 = header missing a required column, 0 = not a header. */
static int classify_header(char **fields, size_t nfields, int *time_i, int *t1_i, int *t2_i,
                           int *valid_i, char *error, size_t error_len)
{
    size_t i;
    int named = 0;

    *time_i = -1;
    *t1_i = -1;
    *t2_i = -1;
    *valid_i = -1;
    for (i = 0; i < nfields; i++) {
        ColumnRole role = column_role(fields[i]);
        if (role == ROLE_TIME && *time_i < 0) {
            *time_i = (int)i;
            named++;
        } else if (role == ROLE_T1 && *t1_i < 0) {
            *t1_i = (int)i;
            named++;
        } else if (role == ROLE_T2 && *t2_i < 0) {
            *t2_i = (int)i;
            named++;
        } else if (role == ROLE_VALID && *valid_i < 0) {
            *valid_i = (int)i;
        }
    }
    if (named < 2) {
        return 0;
    }
    if (*time_i < 0) {
        set_error(error, error_len, "The file has no time field");
        return -1;
    }
    if (*t1_i < 0) {
        set_error(error, error_len, "The file has no T1 column");
        return -1;
    }
    if (*t2_i < 0) {
        set_error(error, error_len, "The file has no T2 column");
        return -1;
    }
    return 1;
}

static const char *field_at(char **fields, size_t nfields, int index)
{
    if (index < 0 || (size_t)index >= nfields) {
        return "";
    }
    return fields[index];
}

static bool row_from_mapped(char **fields, size_t nfields, int time_i, int t1_i, int t2_i,
                            int valid_i, ParsedRow *row)
{
    const char *time_text;
    double clock = 0.0;
    double elapsed = 0.0;
    int t1_kind;
    int t2_kind;
    bool forced_invalid = false;

    if (time_i < 0 || (size_t)time_i >= nfields) {
        return false;
    }
    memset(row, 0, sizeof(*row));
    row->t1 = nan("");
    row->t2 = nan("");
    time_text = fields[time_i];
    if (parse_hms(time_text, &clock)) {
        row->from_clock = true;
        row->clock_seconds = clock;
    } else if (parse_number(time_text, &elapsed)) {
        row->elapsed = elapsed;
    } else {
        return false;
    }
    t1_kind = channel_kind(field_at(fields, nfields, t1_i), &row->t1);
    t2_kind = channel_kind(field_at(fields, nfields, t2_i), &row->t2);
    if (valid_i >= 0 && (size_t)valid_i < nfields && strcmp(fields[valid_i], "0") == 0) {
        forced_invalid = true;
    }
    if (forced_invalid || t1_kind != 1 || t2_kind != 1) {
        if (forced_invalid || t1_kind != 1) {
            row->t1 = nan("");
        }
        if (forced_invalid || t2_kind != 1) {
            row->t2 = nan("");
        }
        row->valid = false;
    } else {
        row->valid = true;
    }
    return true;
}

static bool row_from_fields(char **fields, size_t nfields, ParsedRow *row)
{
    int time_index = -1;
    size_t i;

    memset(row, 0, sizeof(*row));
    row->t1 = nan("");
    row->t2 = nan("");

    for (i = 0; i < nfields; i++) {
        double clock = 0.0;
        if (parse_hms(fields[i], &clock)) {
            time_index = (int)i;
            row->clock_seconds = clock;
            break;
        }
    }

    if (time_index >= 0) {
        size_t t1_index = (size_t)time_index + 1u;
        size_t t2_index = (size_t)time_index + 2u;
        int t1_kind;
        int t2_kind;
        if (t2_index >= nfields) {
            return false;
        }
        t1_kind = channel_kind(fields[t1_index], &row->t1);
        t2_kind = channel_kind(fields[t2_index], &row->t2);
        /* A metadata token such as "CH1" is not a temperature. */
        if (t1_kind < 0 || t2_kind < 0) {
            return false;
        }
        row->valid = t1_kind == 1 && t2_kind == 1;
        row->from_clock = true;
        return true;
    }

    if (nfields >= 4u && !parse_number(fields[0], &row->elapsed)) {
        double elapsed = 0.0;
        const char *flag = fields[nfields - 1u];
        bool flag_is_valid = strcmp(flag, "0") == 0 || strcmp(flag, "1") == 0;
        if (parse_number(fields[1], &elapsed) && flag_is_valid) {
            bool forced_invalid = strcmp(flag, "0") == 0;
            int t1_kind = forced_invalid ? 0 : channel_kind(fields[2], &row->t1);
            int t2_kind = forced_invalid ? 0 : channel_kind(nfields >= 4u ? fields[3] : "", &row->t2);
            if (!forced_invalid && (t1_kind < 0 || t2_kind < 0)) {
                return false;
            }
            row->elapsed = elapsed;
            if (forced_invalid || t1_kind != 1) {
                row->t1 = nan("");
            }
            if (forced_invalid || t2_kind != 1) {
                row->t2 = nan("");
            }
            row->valid = !forced_invalid && t1_kind == 1 && t2_kind == 1;
            return true;
        }
    }

    if (nfields >= 3u && parse_number(fields[0], &row->elapsed)) {
        int t1_kind = channel_kind(fields[1], &row->t1);
        int t2_kind = channel_kind(fields[2], &row->t2);
        if (t1_kind < 0 || t2_kind < 0) {
            return false;
        }
        row->valid = t1_kind == 1 && t2_kind == 1;
        return true;
    }
    return false;
}

static bool push_row(ParsedRow **rows, size_t *count, size_t *capacity, ParsedRow row,
                     char *error, size_t error_len)
{
    if (*count == *capacity) {
        size_t grown = (*capacity == 0u) ? 256u : (*capacity * 2u);
        ParsedRow *fresh;
        if (grown > 2000000u) {
            set_error(error, error_len, "Replay file is too large");
            return false;
        }
        fresh = realloc(*rows, grown * sizeof(*fresh));
        if (fresh == NULL) {
            set_error(error, error_len, "Out of memory while reading the replay file");
            return false;
        }
        *rows = fresh;
        *capacity = grown;
    }
    (*rows)[*count] = row;
    (*count)++;
    return true;
}

static void assign_clock_elapsed(ParsedRow *rows, size_t count)
{
    size_t i;
    double previous = 0.0;
    double elapsed = 0.0;
    bool have_previous = false;

    for (i = 0; i < count; i++) {
        if (!rows[i].from_clock) {
            return;
        }
    }
    for (i = 0; i < count; i++) {
        double delta;
        if (!have_previous) {
            previous = rows[i].clock_seconds;
            rows[i].elapsed = 0.0;
            have_previous = true;
            continue;
        }
        delta = rows[i].clock_seconds - previous;
        if (delta < -12.0 * 3600.0) {
            delta += 24.0 * 3600.0;
        }
        elapsed += delta;
        rows[i].elapsed = elapsed;
        previous = rows[i].clock_seconds;
    }
}

static void normalize_and_hold(ParsedRow *rows, size_t count, ReplayParseReport *report)
{
    size_t i;
    double origin;
    double previous;

    if (count == 0u) {
        return;
    }
    origin = rows[0].elapsed;
    previous = 0.0;
    for (i = 0; i < count; i++) {
        double elapsed = rows[i].elapsed - origin;
        if (i > 0u && elapsed + 1e-4 < previous) {
            report->backward_timestamps++;
            elapsed = previous;
        } else if (i > 0u && fabs(elapsed - previous) <= 1e-4) {
            report->duplicate_timestamps++;
            elapsed = previous;
        }
        if (elapsed < 0.0) {
            elapsed = 0.0;
        }
        rows[i].elapsed = elapsed;
        previous = elapsed;
    }
}

static void build_summary(ReplayParseReport *report)
{
    char loaded[32];
    char sentence[256];
    size_t used;

    format_count(loaded, sizeof loaded, report->loaded);
    snprintf(sentence, sizeof sentence, "Loaded %s samples.", loaded);
    used = strlen(sentence);
    if (report->skipped > 0u && used + 1u < sizeof sentence) {
        char count[32];
        format_count(count, sizeof count, report->skipped);
        snprintf(sentence + used, sizeof sentence - used, " Skipped %s malformed rows.", count);
        used = strlen(sentence);
    }
    if (report->duplicate_timestamps > 0u && used + 1u < sizeof sentence) {
        char count[32];
        format_count(count, sizeof count, report->duplicate_timestamps);
        snprintf(sentence + used, sizeof sentence - used, " %s duplicate timestamps.", count);
        used = strlen(sentence);
    }
    if (report->backward_timestamps > 0u && used + 1u < sizeof sentence) {
        char count[32];
        format_count(count, sizeof count, report->backward_timestamps);
        snprintf(sentence + used, sizeof sentence - used,
                 " %s timestamps were out of order.", count);
    }
    snprintf(report->summary, sizeof report->summary, "%s", sentence);
}

void replay_samples_free(ReplaySample *samples)
{
    free(samples);
}

/* Portable replacement for POSIX getline. The caller frees *buffer. */
static char *read_line(FILE *file, char **buffer, size_t *capacity)
{
    size_t length = 0;
    int ch;

    if (*buffer == NULL || *capacity == 0u) {
        *capacity = 256u;
        *buffer = malloc(*capacity);
        if (*buffer == NULL) {
            return NULL;
        }
    }
    while ((ch = fgetc(file)) != EOF) {
        if (length + 1u >= *capacity) {
            size_t grown = *capacity * 2u;
            char *fresh = realloc(*buffer, grown);
            if (fresh == NULL) {
                return NULL;
            }
            *buffer = fresh;
            *capacity = grown;
        }
        (*buffer)[length++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (length == 0u && ch == EOF) {
        return NULL;
    }
    (*buffer)[length] = '\0';
    return *buffer;
}

int replay_parse_path(const char *path, ReplaySample **out_samples, size_t *out_count,
                      ReplayParseReport *report, char *error, size_t error_len)
{
    FILE *file;
    char *line = NULL;
    size_t line_cap = 0;
    ParsedRow *rows = NULL;
    size_t count = 0;
    size_t capacity = 0;
    ReplaySample *samples;
    size_t i;
    bool first_line = true;
    bool have_header = false;
    int time_i = -1;
    int t1_i = -1;
    int t2_i = -1;
    int valid_i = -1;
    ReplayParseReport local;

    memset(&local, 0, sizeof local);
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
    }
    if (out_samples == NULL || out_count == NULL) {
        set_error(error, error_len, "Internal replay parser error");
        return -1;
    }
    *out_samples = NULL;
    *out_count = 0;
    if (path == NULL || path[0] == '\0') {
        set_error(error, error_len, "No replay file selected");
        return -1;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        set_error(error, error_len, "Could not open the replay file");
        return -1;
    }

    while (read_line(file, &line, &line_cap) != NULL) {
        char *fields[32];
        size_t nfields;
        size_t length;
        ParsedRow row;
        bool parsed;
        bool delimited;

        if (first_line) {
            first_line = false;
            if ((unsigned char)line[0] == 0xEFu &&
                (unsigned char)line[1] == 0xBBu &&
                (unsigned char)line[2] == 0xBFu) {
                memmove(line, line + 3, strlen(line + 3) + 1u);
            }
        }
        length = strlen(line);
        while (length > 0u && (line[length - 1u] == '\n' || line[length - 1u] == '\r')) {
            line[--length] = '\0';
        }
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }
        delimited = strchr(line, '\t') != NULL || strchr(line, ',') != NULL;
        nfields = split_fields(line, fields, 32);
        if (nfields == 0u) {
            continue;
        }
        if (!have_header) {
            int header = classify_header(fields, nfields, &time_i, &t1_i, &t2_i, &valid_i,
                                         error, error_len);
            if (header < 0) {
                free(rows);
                free(line);
                fclose(file);
                return -1;
            }
            if (header > 0) {
                have_header = true;
                continue;
            }
        }
        if (!have_header) {
            parsed = row_from_fields(fields, nfields, &row);
        } else if ((size_t)time_i < nfields && (size_t)t1_i < nfields &&
                   (size_t)t2_i < nfields) {
            parsed = row_from_mapped(fields, nfields, time_i, t1_i, t2_i, valid_i, &row);
        } else {
            /* A short elapsed,t1,t2 line after a wider header is still a sample. */
            parsed = row_from_fields(fields, nfields, &row);
        }
        if (!parsed) {
            if (have_header && delimited) {
                local.skipped++;
            }
            continue;
        }
        if (!push_row(&rows, &count, &capacity, row, error, error_len)) {
            free(rows);
            free(line);
            fclose(file);
            return -1;
        }
    }
    free(line);
    fclose(file);

    if (count == 0u) {
        free(rows);
        set_error(error, error_len, "No measurements found in the replay file");
        if (report != NULL) {
            report->skipped = local.skipped;
        }
        return -1;
    }

    assign_clock_elapsed(rows, count);
    local.loaded = count;
    normalize_and_hold(rows, count, &local);
    build_summary(&local);

    samples = calloc(count, sizeof(*samples));
    if (samples == NULL) {
        free(rows);
        set_error(error, error_len, "Out of memory while reading the replay file");
        return -1;
    }
    for (i = 0; i < count; i++) {
        samples[i].elapsed_seconds = rows[i].elapsed;
        samples[i].t1 = rows[i].t1;
        samples[i].t2 = rows[i].t2;
        samples[i].valid = rows[i].valid;
    }
    free(rows);
    *out_samples = samples;
    *out_count = count;
    if (report != NULL) {
        *report = local;
    }
    if (error != NULL && error_len > 0u) {
        error[0] = '\0';
    }
    return 0;
}
