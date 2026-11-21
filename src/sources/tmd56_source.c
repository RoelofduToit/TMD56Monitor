#define _POSIX_C_SOURCE 200809L

#include "sources/tmd56_source.h"

#include <libserialport.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Live TMD-56 / HH806AU reading.
 *
 * The meter answers "#0A0000NA2\r\n" with 16 bytes beginning 0x3E 0x0F.
 * Bytes 5–6 and 10–11 are signed big-endian tenths of a degree. Artisan
 * reads the same bytes and divides by 10. A high bit clear is therefore
 * the same number that program shows. The sign bit is kept so a negative
 * temperature stays negative instead of becoming several thousand degrees.
 * 0x8000 and anything outside -250 °C .. 3276.7 °C is left blank.
 */

#define TMD_BAUD 19200
#define TMD_FRAME_LEN 16
#define TMD_HEADER0 0x3Eu
#define TMD_HEADER1 0x0Fu
#define TMD_T1_OFFSET 5
#define TMD_T2_OFFSET 10
#define TMD_TENTHS_MIN (-2500)
#define TMD_TENTHS_MAX 32767
#define TMD_PERIOD_DEFAULT_S 0.5
#define TMD_RX_CAP 96

static const unsigned char TMD_QUERY[] = "#0A0000NA2\r\n";

struct Tmd56Source {
    MeasurementSource base;
    char port_name[256];
    struct sp_port *port;
    double period_s;
    double origin_mono;
    double next_query_mono;
    double reply_deadline_mono;
    bool waiting_reply;
    bool have_reading;
    unsigned char rx[TMD_RX_CAP];
    size_t rx_len;
};

static void tmd_note(Tmd56Source *source, const char *text, bool attention)
{
    snprintf(source->base.status, sizeof source->base.status, "%s", text != NULL ? text : "");
    source->base.attention = attention;
}

static void tmd_set_sp_error(Tmd56Source *source, const char *what)
{
    char *message = sp_last_error_message();
    snprintf(source->base.status, sizeof source->base.status, "%s: %s",
             what, (message != NULL && message[0] != '\0') ? message : "serial error");
    source->base.attention = true;
    if (message != NULL) {
        sp_free_error_message(message);
    }
}

static void tmd_release_port(Tmd56Source *source)
{
    if (source->port == NULL) {
        return;
    }
    sp_close(source->port);
    sp_free_port(source->port);
    source->port = NULL;
}

static double tmd_channel(unsigned char hi, unsigned char lo)
{
    int raw = ((int)hi << 8) | (int)lo;
    if (raw >= 0x8000) {
        raw -= 0x10000;
    }
    if (raw < TMD_TENTHS_MIN || raw > TMD_TENTHS_MAX) {
        return nan("");
    }
    return (double)raw / 10.0;
}

int tmd56_decode_frame(const unsigned char *frame, size_t length, double *t1, double *t2)
{
    if (t1 != NULL) {
        *t1 = nan("");
    }
    if (t2 != NULL) {
        *t2 = nan("");
    }
    if (frame == NULL || length < TMD_FRAME_LEN ||
        frame[0] != TMD_HEADER0 || frame[1] != TMD_HEADER1) {
        return -1;
    }
    if (t1 != NULL) {
        *t1 = tmd_channel(frame[TMD_T1_OFFSET], frame[TMD_T1_OFFSET + 1]);
    }
    if (t2 != NULL) {
        *t2 = tmd_channel(frame[TMD_T2_OFFSET], frame[TMD_T2_OFFSET + 1]);
    }
    return 0;
}

int tmd56_extract_frame(const unsigned char *buf, size_t length,
                        unsigned char out[16], size_t *used)
{
    size_t index;

    if (used != NULL) {
        *used = 0;
    }
    if (buf == NULL || length == 0) {
        return 0;
    }
    for (index = 0; index + 1 < length; index++) {
        if (buf[index] != TMD_HEADER0 || buf[index + 1] != TMD_HEADER1) {
            continue;
        }
        if (index + TMD_FRAME_LEN > length) {
            if (used != NULL) {
                *used = index;
            }
            return 0;
        }
        if (out != NULL) {
            memcpy(out, buf + index, TMD_FRAME_LEN);
        }
        if (used != NULL) {
            *used = index + TMD_FRAME_LEN;
        }
        return 1;
    }
    if (used != NULL) {
        *used = (buf[length - 1] == TMD_HEADER0) ? (length - 1) : length;
    }
    return 0;
}

static void tmd_consume_bytes(unsigned char *buf, size_t *length, size_t used)
{
    if (buf == NULL || length == NULL || used == 0 || *length == 0) {
        return;
    }
    if (used > *length) {
        used = *length;
    }
    memmove(buf, buf + used, *length - used);
    *length -= used;
}

int tmd56_consume_frames(unsigned char *buf, size_t *length, unsigned char newest[16])
{
    int found = 0;

    if (buf == NULL || length == NULL) {
        return 0;
    }
    for (;;) {
        unsigned char frame[TMD_FRAME_LEN];
        size_t used = 0;
        int ready = tmd56_extract_frame(buf, *length, frame, &used);
        if (ready == 1) {
            if (newest != NULL) {
                memcpy(newest, frame, TMD_FRAME_LEN);
            }
            found = 1;
            tmd_consume_bytes(buf, length, used);
            continue;
        }
        tmd_consume_bytes(buf, length, used);
        break;
    }
    return found;
}

size_t tmd56_list_ports(TmdPort *out, size_t max_count)
{
    struct sp_port **list = NULL;
    TmdPort found[32];
    size_t count = 0;
    size_t written = 0;
    size_t index;
    int cursor;

    if (out == NULL || max_count == 0) {
        return 0;
    }
    if (sp_list_ports(&list) != SP_OK || list == NULL) {
        return 0;
    }
    for (cursor = 0; list[cursor] != NULL && count < (sizeof found / sizeof found[0]); cursor++) {
        const char *name = sp_get_port_name(list[cursor]);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        snprintf(found[count].name, sizeof found[count].name, "%s", name);
        found[count].usb = sp_get_port_transport(list[cursor]) == SP_TRANSPORT_USB;
        count++;
    }
    sp_free_port_list(list);

    for (index = 0; index < count && written < max_count; index++) {
        if (found[index].usb) {
            out[written++] = found[index];
        }
    }
    for (index = 0; index < count && written < max_count; index++) {
        if (!found[index].usb) {
            out[written++] = found[index];
        }
    }
    return written;
}

static int tmd_open(MeasurementSource *self, const char *target)
{
    Tmd56Source *source = (Tmd56Source *)self;
    enum sp_return result;

    tmd_release_port(source);
    self->is_open = false;
    self->attention = false;
    source->waiting_reply = false;
    source->rx_len = 0;

    if (target == NULL || target[0] == '\0') {
        tmd_note(source, "Choose a serial port.", true);
        return -1;
    }
    snprintf(source->port_name, sizeof source->port_name, "%s", target);

    result = sp_get_port_by_name(source->port_name, &source->port);
    if (result != SP_OK || source->port == NULL) {
        source->port = NULL;
        tmd_set_sp_error(source, "Could not open the serial port");
        return -1;
    }
    result = sp_open(source->port, SP_MODE_READ_WRITE);
    if (result != SP_OK) {
        char *message = sp_last_error_message();
        int denied = message != NULL &&
                     (strstr(message, "denied") != NULL || strstr(message, "Denied") != NULL ||
                      strstr(message, "ermission") != NULL);
        if (denied) {
            snprintf(self->status, sizeof self->status,
                     "Could not open %s. Add your user to the dialout group, then log in again.",
                     source->port_name);
        } else if (message != NULL && message[0] != '\0') {
            snprintf(self->status, sizeof self->status, "Could not open the serial port: %s", message);
        } else {
            snprintf(self->status, sizeof self->status, "Could not open the serial port.");
        }
        self->attention = true;
        if (message != NULL) {
            sp_free_error_message(message);
        }
        sp_free_port(source->port);
        source->port = NULL;
        return -1;
    }

    result = sp_set_baudrate(source->port, TMD_BAUD);
    if (result == SP_OK) {
        result = sp_set_bits(source->port, 8);
    }
    if (result == SP_OK) {
        result = sp_set_parity(source->port, SP_PARITY_EVEN);
    }
    if (result == SP_OK) {
        result = sp_set_stopbits(source->port, 1);
    }
    if (result == SP_OK) {
        result = sp_set_flowcontrol(source->port, SP_FLOWCONTROL_NONE);
    }
    if (result != SP_OK) {
        tmd_set_sp_error(source, "Could not configure 19200 8E1");
        tmd_release_port(source);
        return -1;
    }
    /* USB-serial adapters expect these asserted. Ignore adapters that cannot set them. */
    (void)sp_set_dtr(source->port, SP_DTR_ON);
    (void)sp_set_rts(source->port, SP_RTS_ON);

    self->is_open = true;
    tmd_note(source, "Port open", false);
    return 0;
}

static void tmd_close(MeasurementSource *self)
{
    Tmd56Source *source = (Tmd56Source *)self;
    tmd_release_port(source);
    self->is_open = false;
    self->running = false;
    source->waiting_reply = false;
    source->rx_len = 0;
    tmd_note(source, "Port closed", false);
}

static int tmd_start(MeasurementSource *self)
{
    Tmd56Source *source = (Tmd56Source *)self;
    if (!self->is_open || source->port == NULL) {
        tmd_note(source, "Choose a serial port.", true);
        return -1;
    }
    source->origin_mono = tmd_monotonic_seconds();
    source->next_query_mono = source->origin_mono;
    source->waiting_reply = false;
    source->have_reading = false;
    source->rx_len = 0;
    self->running = true;
    tmd_note(source, "Waiting for the meter", true);
    return 0;
}

static void tmd_stop(MeasurementSource *self)
{
    Tmd56Source *source = (Tmd56Source *)self;
    self->running = false;
    source->waiting_reply = false;
    source->rx_len = 0;
    if (self->is_open) {
        tmd_note(source, "Port open", false);
    }
}

static double tmd_reply_budget(const Tmd56Source *source)
{
    double budget = source->period_s * 0.8;
    if (budget > 0.40) {
        budget = 0.40;
    }
    if (budget < 0.15) {
        budget = 0.15;
    }
    return budget;
}

static int tmd_read_pending(Tmd56Source *source)
{
    unsigned char chunk[64];
    int passes;

    /* A few chunks per UI tick. More data waits for the next poll. */
    for (passes = 0; passes < 8; passes++) {
        enum sp_return got = sp_nonblocking_read(source->port, chunk, sizeof chunk);
        size_t count;
        if (got == 0) {
            return 0;
        }
        if (got < 0) {
            tmd_set_sp_error(source, "Serial read failed");
            return -1;
        }
        count = (size_t)got;
        if (source->rx_len + count > sizeof source->rx) {
            size_t drop = source->rx_len + count - sizeof source->rx;
            if (drop >= source->rx_len) {
                source->rx_len = 0;
            } else {
                memmove(source->rx, source->rx + drop, source->rx_len - drop);
                source->rx_len -= drop;
            }
        }
        memcpy(source->rx + source->rx_len, chunk, count);
        source->rx_len += count;
    }
    return 0;
}

static int tmd_emit_latest(Tmd56Source *source, TemperatureMeasurement *out, double now)
{
    unsigned char newest[TMD_FRAME_LEN];
    double t1;
    double t2;
    double elapsed;

    if (tmd56_consume_frames(source->rx, &source->rx_len, newest) != 1) {
        return 0;
    }
    if (tmd56_decode_frame(newest, TMD_FRAME_LEN, &t1, &t2) != 0) {
        return 0;
    }
    elapsed = now - source->origin_mono;
    if (elapsed < 0.0) {
        elapsed = 0.0;
    }
    measurement_set(out, t1, t2, elapsed, true);
    source->waiting_reply = false;
    source->have_reading = true;
    source->next_query_mono = now + source->period_s;
    tmd_note(source, "Reading the meter", false);
    return 1;
}

static int tmd_send_query(Tmd56Source *source, double now)
{
    enum sp_return written;

    source->rx_len = 0;
    (void)sp_flush(source->port, SP_BUF_INPUT);
    written = sp_blocking_write(source->port, TMD_QUERY, sizeof TMD_QUERY - 1u, 40);
    if (written < 0) {
        tmd_set_sp_error(source, "Could not query the meter");
        return -1;
    }
    if ((size_t)written != sizeof TMD_QUERY - 1u) {
        source->waiting_reply = false;
        source->next_query_mono = now + source->period_s;
        if (!source->have_reading) {
            tmd_note(source, "Waiting for the meter", true);
        }
        return 0;
    }
    source->waiting_reply = true;
    source->reply_deadline_mono = now + tmd_reply_budget(source);
    if (!source->have_reading) {
        tmd_note(source, "Waiting for the meter", true);
    }
    return 0;
}

static int tmd_get(MeasurementSource *self, TemperatureMeasurement *out)
{
    Tmd56Source *source = (Tmd56Source *)self;
    double now;

    if (!self->running || source->port == NULL || out == NULL) {
        return 0;
    }
    now = tmd_monotonic_seconds();
    if (tmd_read_pending(source) != 0) {
        return -1;
    }
    if (source->waiting_reply) {
        if (tmd_emit_latest(source, out, now) == 1) {
            return 1;
        }
        if (now >= source->reply_deadline_mono) {
            source->waiting_reply = false;
            source->rx_len = 0;
            source->next_query_mono = now + source->period_s;
            tmd_note(source, "No reply from the meter", true);
        }
        return 0;
    }
    if (now + 1e-4 >= source->next_query_mono) {
        if (tmd_send_query(source, now) != 0) {
            return -1;
        }
        if (tmd_read_pending(source) != 0) {
            return -1;
        }
        if (tmd_emit_latest(source, out, now) == 1) {
            return 1;
        }
    }
    return 0;
}

static const char *tmd_status(const MeasurementSource *self)
{
    return self->status;
}

static bool tmd_finished(const MeasurementSource *self)
{
    (void)self;
    return false;
}

static const MeasurementSourceVTable TMD_VTABLE = {
    .open = tmd_open,
    .close = tmd_close,
    .start = tmd_start,
    .stop = tmd_stop,
    .get_measurement = tmd_get,
    .status = tmd_status,
    .finished = tmd_finished,
};

Tmd56Source *tmd56_source_create(void)
{
    Tmd56Source *source = calloc(1, sizeof(*source));
    if (source == NULL) {
        return NULL;
    }
    source->base.vtable = &TMD_VTABLE;
    source->period_s = TMD_PERIOD_DEFAULT_S;
    tmd_note(source, "Choose a serial port.", false);
    return source;
}

void tmd56_source_destroy(Tmd56Source *source)
{
    if (source == NULL) {
        return;
    }
    tmd_release_port(source);
    free(source);
}

MeasurementSource *tmd56_source_base(Tmd56Source *source)
{
    return source == NULL ? NULL : &source->base;
}

void tmd56_source_set_period(Tmd56Source *source, double seconds)
{
    if (source == NULL) {
        return;
    }
    if (seconds < 0.1 || seconds > 5.0) {
        return;
    }
    source->period_s = seconds;
}
