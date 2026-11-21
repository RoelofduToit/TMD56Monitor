#include "measurement.h"
#include "sources/tmd56_source.h"

#include <math.h>
#include <stdio.h>
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

static void put_tenths(unsigned char *at, int tenths)
{
    unsigned raw = (unsigned)tenths & 0xFFFFu;
    at[0] = (unsigned char)((raw >> 8) & 0xFFu);
    at[1] = (unsigned char)(raw & 0xFFu);
}

static void blank_frame(unsigned char frame[16])
{
    memset(frame, 0, 16);
    frame[0] = 0x3E;
    frame[1] = 0x0F;
}

static void expect_channel(const char *name, double value, double expected)
{
    if (!isfinite(value) || fabs(value - expected) > 1e-9) {
        fprintf(stderr, "FAIL %s got %.6f expected %.1f\n", name, value, expected);
        failures++;
    }
}

static void expect_blank(const char *name, double value)
{
    if (isfinite(value)) {
        fprintf(stderr, "FAIL %s should be blank, got %.6f\n", name, value);
        failures++;
    }
}

static void test_known_readings(void)
{
    unsigned char frame[16];
    double t1;
    double t2;

    blank_frame(frame);
    put_tenths(frame + 5, 250);
    put_tenths(frame + 10, 246);
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == 0);
    expect_channel("25.0", t1, 25.0);
    expect_channel("24.6", t2, 24.6);

    put_tenths(frame + 5, -30);
    put_tenths(frame + 10, 1000);
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == 0);
    expect_channel("-3.0", t1, -3.0);
    expect_channel("100.0", t2, 100.0);

    put_tenths(frame + 5, -2100);
    put_tenths(frame + 10, 17670);
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == 0);
    expect_channel("-210.0", t1, -210.0);
    expect_channel("1767.0", t2, 1767.0);

    put_tenths(frame + 5, 32767);
    put_tenths(frame + 10, 20000);
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == 0);
    expect_channel("3276.7", t1, 3276.7);
    expect_channel("2000.0", t2, 2000.0);

    /* 0xFFFF is -0.1 °C, not an overload token. */
    put_tenths(frame + 5, -1);
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == 0);
    expect_channel("-0.1", t1, -0.1);
}

static void test_blank_channels(void)
{
    unsigned char frame[16];
    double t1 = 1.0;
    double t2 = 1.0;
    TemperatureMeasurement sample;

    blank_frame(frame);
    frame[5] = 0x80;
    frame[6] = 0x00;
    put_tenths(frame + 10, 250);
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == 0);
    expect_blank("0x8000", t1);
    expect_channel("other channel", t2, 25.0);
    measurement_set(&sample, t1, t2, 0.0, true);
    EXPECT(!sample.valid);
    EXPECT(!isfinite(sample.t1));
    EXPECT(fabs(sample.t2 - 25.0) < 1e-9);
    EXPECT(!isfinite(sample.delta_t));

    put_tenths(frame + 5, -2501);
    put_tenths(frame + 10, 250);
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == 0);
    expect_blank("below -250", t1);
    expect_channel("still 25", t2, 25.0);

    frame[0] = 0x00;
    t1 = 5.0;
    t2 = 5.0;
    EXPECT(tmd56_decode_frame(frame, 16, &t1, &t2) == -1);
    expect_blank("bad header t1", t1);
    expect_blank("bad header t2", t2);
    EXPECT(tmd56_decode_frame(frame, 15, &t1, &t2) == -1);
    EXPECT(tmd56_decode_frame(NULL, 16, &t1, &t2) == -1);
}

static void test_stream(void)
{
    unsigned char first[16];
    unsigned char second[16];
    unsigned char buf[64];
    unsigned char out[16];
    unsigned char newest[16];
    size_t used = 99;
    size_t length;

    blank_frame(first);
    blank_frame(second);
    put_tenths(first + 5, 250);
    put_tenths(second + 5, 1000);

    memset(buf, 0x11, sizeof buf);
    memcpy(buf + 3, first, 16);
    EXPECT(tmd56_extract_frame(buf, 3 + 16, out, &used) == 1);
    EXPECT(used == 3u + 16u);
    EXPECT(memcmp(out, first, 16) == 0);

    buf[0] = 0x00;
    buf[1] = 0x3E;
    buf[2] = 0x0F;
    used = 99;
    EXPECT(tmd56_extract_frame(buf, 3, out, &used) == 0);
    EXPECT(used == 1u);

    buf[0] = 0x01;
    buf[1] = 0x02;
    buf[2] = 0x3E;
    used = 99;
    EXPECT(tmd56_extract_frame(buf, 3, out, &used) == 0);
    EXPECT(used == 2u);

    memcpy(buf, first, 16);
    memcpy(buf + 16, second, 16);
    length = 32;
    EXPECT(tmd56_consume_frames(buf, &length, newest) == 1);
    EXPECT(length == 0u);
    EXPECT(memcmp(newest, second, 16) == 0);

    memcpy(buf, first, 16);
    buf[16] = 0x3E;
    length = 17;
    EXPECT(tmd56_consume_frames(buf, &length, newest) == 1);
    EXPECT(length == 1u);
    EXPECT(buf[0] == 0x3E);
}

static void test_port_list(void)
{
    TmdPort ports[16];
    size_t count;
    size_t index;

    EXPECT(tmd56_list_ports(NULL, 4) == 0u);
    EXPECT(tmd56_list_ports(ports, 0) == 0u);
    count = tmd56_list_ports(ports, sizeof ports / sizeof ports[0]);
    EXPECT(count <= sizeof ports / sizeof ports[0]);
    for (index = 0; index < count; index++) {
        EXPECT(ports[index].name[0] != '\0');
    }
}

int main(void)
{
    test_known_readings();
    test_blank_channels();
    test_stream();
    test_port_list();
    if (failures != 0) {
        fprintf(stderr, "%d frame checks failed\n", failures);
        return 1;
    }
    printf("tmd56 frame checks passed\n");
    return 0;
}
