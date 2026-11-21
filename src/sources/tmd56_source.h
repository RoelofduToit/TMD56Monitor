#ifndef TMD56_TMD56_SOURCE_H
#define TMD56_TMD56_SOURCE_H

#include "sources/measurement_source.h"

#include <stddef.h>

/*
 * Amprobe TMD-56 / Omega HH806AU live reading.
 *
 * 19200 baud, 8 data bits, even parity, 1 stop bit, no flow control.
 * The program sends "#0A0000NA2" and reads a 16-byte frame that starts
 * 0x3E 0x0F. T1 is bytes 5–6 and T2 is bytes 10–11, signed, in tenths
 * of a degree. That is the query used by Artisan for this meter.
 * Positive readings match that software. The meter must be set to °C.
 * A channel outside the meter's range is left blank and is never stored as 0.
 */

typedef struct Tmd56Source Tmd56Source;

typedef struct {
    char name[256];
    bool usb;
} TmdPort;

Tmd56Source *tmd56_source_create(void);
void tmd56_source_destroy(Tmd56Source *source);
MeasurementSource *tmd56_source_base(Tmd56Source *source);

/* Sample period in seconds. Values outside 0.1–5 s are ignored. */
void tmd56_source_set_period(Tmd56Source *source, double seconds);

/*
 * Decodes one 16-byte frame. Returns 0 when the header matches.
 * *t1 and *t2 are degrees or NAN. Returns -1 for any other buffer.
 */
int tmd56_decode_frame(const unsigned char *frame, size_t length, double *t1, double *t2);

/*
 * Finds the next 16-byte frame in a byte stream.
 * Returns 1 and copies it when a full frame is present.
 * Returns 0 when more bytes are needed.
 * *used is how many leading bytes the caller should drop.
 */
int tmd56_extract_frame(const unsigned char *buf, size_t length,
                        unsigned char out[16], size_t *used);

/* Drops consumed bytes. Copies the newest complete frame. Returns 1 when one was found. */
int tmd56_consume_frames(unsigned char *buf, size_t *length, unsigned char newest[16]);

/* USB ports are listed first. Returns how many names were written. */
size_t tmd56_list_ports(TmdPort *out, size_t max_count);

#endif
