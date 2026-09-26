#define _POSIX_C_SOURCE 200809L

#include "sources/tmd56_source.h"

#include <libserialport.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * UNVERIFIED protocol notes, kept in one place so the GUI and logger
 * do not grow a private copy of them:
 *
 *   19200 baud, 8 data bits, even parity, 1 stop bit, no flow control
 *   reported query:      #0A0000NA2\r\n
 *   reported frame size: 16 bytes
 *   reported header:     0x3E 0x0F
 *   reported T1 bytes:   around offsets 5-6
 *   reported T2 bytes:   around offsets 10-11
 *
 * Status-byte meanings are not documented here and are not guessed.
 * tmd56_decode_frame_unverified() refuses to emit a temperature until
 * a capture from the real meter confirms the layout.
 */

#define TMD56_BAUD_UNVERIFIED 19200
#define TMD56_FRAME_LEN_UNVERIFIED 16
#define TMD56_HEADER0_UNVERIFIED 0x3Eu
#define TMD56_HEADER1_UNVERIFIED 0x0Fu

static const char TMD56_QUERY_UNVERIFIED[] = "#0A0000NA2\r\n";

struct Tmd56Source {
    MeasurementSource base;
    char port_name[256];
    struct sp_port *port;
    unsigned long bytes_seen;
    unsigned char raw[TMD56_FRAME_LEN_UNVERIFIED];
    size_t raw_len;
};

const char *tmd56_reported_query_unverified(void)
{
    return TMD56_QUERY_UNVERIFIED;
}

static void tmd_set_sp_error(Tmd56Source *source, const char *what)
{
    char *message = sp_last_error_message();
    snprintf(source->base.status, sizeof source->base.status, "%s: %s",
             what, (message != NULL && message[0] != '\0') ? message : "serial error");
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

/*
 * Returns -1 until the frame layout is confirmed on the real instrument.
 * The constants are referenced so the reported layout stays next to the
 * decoder instead of drifting into a comment nobody compiles.
 */
static int tmd56_decode_frame_unverified(const unsigned char *frame, size_t length,
                                         TemperatureMeasurement *out)
{
    (void)out;
    if (frame == NULL || length < TMD56_FRAME_LEN_UNVERIFIED) {
        return -1;
    }
    if (frame[0] != TMD56_HEADER0_UNVERIFIED || frame[1] != TMD56_HEADER1_UNVERIFIED) {
        return -1;
    }
    /* Offsets 5-6 and 10-11 are NOT interpreted. Doing so would invent data. */
    return -1;
}

static void tmd_describe(Tmd56Source *source)
{
    if (source->port == NULL) {
        snprintf(source->base.status, sizeof source->base.status,
                 "Experimental / unverified — no port open. Temperatures are not simulated.");
        return;
    }
    snprintf(source->base.status, sizeof source->base.status,
             "Experimental / unverified — %s open at %d 8E1, %lu byte(s) received, decoder disabled.",
             source->port_name, TMD56_BAUD_UNVERIFIED, source->bytes_seen);
}

static int tmd_open(MeasurementSource *self, const char *target)
{
    Tmd56Source *source = (Tmd56Source *)self;
    enum sp_return result;

    tmd_release_port(source);
    self->is_open = false;
    source->bytes_seen = 0;
    source->raw_len = 0;

    if (target == NULL || target[0] == '\0') {
        snprintf(self->status, sizeof self->status,
                 "Enter a serial port. Experimental / unverified — no readings will be invented.");
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
        tmd_set_sp_error(source, "Could not open the serial port");
        sp_free_port(source->port);
        source->port = NULL;
        return -1;
    }

    result = sp_set_baudrate(source->port, TMD56_BAUD_UNVERIFIED);
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

    /*
     * The reported query is intentionally not written. Transmitting an
     * unvalidated command would pretend this driver has been tried.
     */
    (void)tmd56_reported_query_unverified();

    self->is_open = true;
    tmd_describe(source);
    return 0;
}

static void tmd_close(MeasurementSource *self)
{
    Tmd56Source *source = (Tmd56Source *)self;
    tmd_release_port(source);
    self->is_open = false;
    self->running = false;
    source->raw_len = 0;
    snprintf(self->status, sizeof self->status,
             "Experimental / unverified — port closed. No temperatures were decoded.");
}

static int tmd_start(MeasurementSource *self)
{
    if (!self->is_open || ((Tmd56Source *)self)->port == NULL) {
        snprintf(self->status, sizeof self->status,
                 "Experimental / unverified — connect a port before starting. No simulated readings.");
        return -1;
    }
    self->running = true;
    tmd_describe((Tmd56Source *)self);
    return 0;
}

static void tmd_stop(MeasurementSource *self)
{
    self->running = false;
    if (self->is_open) {
        tmd_describe((Tmd56Source *)self);
    }
}

static int tmd_get(MeasurementSource *self, TemperatureMeasurement *out)
{
    Tmd56Source *source = (Tmd56Source *)self;
    unsigned char chunk[64];
    enum sp_return result;
    TemperatureMeasurement decoded;

    if (!self->running || source->port == NULL || out == NULL) {
        return 0;
    }

    result = sp_nonblocking_read(source->port, chunk, sizeof chunk);
    if (result < 0) {
        tmd_set_sp_error(source, "Serial read failed");
        return -1;
    }
    if (result > 0) {
        size_t got = (size_t)result;
        size_t copy = got;
        source->bytes_seen += (unsigned long)got;
        if (copy > sizeof source->raw) {
            copy = sizeof source->raw;
        }
        memcpy(source->raw, chunk + (got - copy), copy);
        source->raw_len = copy;
        tmd_describe(source);
    }

    measurement_clear(&decoded);
    if (tmd56_decode_frame_unverified(source->raw, source->raw_len, &decoded) == 0) {
        /* Unreachable until the decoder is honestly implemented. */
        *out = decoded;
        return 1;
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
    snprintf(source->base.status, sizeof source->base.status,
             "Experimental / unverified — temperature decoding is not implemented.");
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
