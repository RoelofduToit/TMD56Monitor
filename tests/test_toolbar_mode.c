#include "ui/toolbar_mode.h"

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void expect_true(const char *name, bool value)
{
    if (!value) {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    }
}

static void expect_false(const char *name, bool value)
{
    expect_true(name, !value);
}

int main(void)
{
    TmdToolbar normal;
    TmdToolbar replay;
    TmdToolbar simulated;
    size_t index;

    tmd_toolbar_for(false, TMD_UI_DEVICE_TMD56, &normal);
    expect_false("normal simulation", normal.simulation);
    expect_true("normal device", normal.show_device);
    expect_false("normal profile hidden", normal.show_profile);
    expect_false("normal badge hidden", normal.show_badge);
    expect_true("normal interval", normal.show_interval);
    expect_true("normal port", normal.show_port);
    expect_false("normal replay hidden", normal.show_replay);
    expect_true("normal acquire", normal.show_acquire);
    expect_true("normal logging", normal.show_logging);

    tmd_toolbar_for(false, TMD_UI_DEVICE_REPLAY, &replay);
    expect_true("replay controls", replay.show_replay);
    expect_false("replay interval hidden", replay.show_interval);
    expect_false("replay acquire hidden", replay.show_acquire);
    expect_false("replay logging hidden", replay.show_logging);
    expect_false("replay port hidden", replay.show_port);
    expect_false("replay profile hidden", replay.show_profile);

    tmd_toolbar_for(true, TMD_UI_DEVICE_TMD56, &simulated);
    expect_true("sim badge", simulated.show_badge);
    expect_true("sim profile", simulated.show_profile);
    expect_false("sim device dropdown hidden", simulated.show_device);
    expect_false("sim replay hidden", simulated.show_replay);
    expect_false("sim port hidden", simulated.show_port);
    expect_true("sim interval", simulated.show_interval);
    expect_true("sim acquire", simulated.show_acquire);

    /* Enabling simulation hides replay even if that device was selected. */
    tmd_toolbar_for(true, TMD_UI_DEVICE_REPLAY, &simulated);
    expect_false("sim overrides replay controls", simulated.show_replay);
    expect_true("sim still acquires", simulated.show_acquire);

    expect_true("default is 500 ms", tmd_interval_seconds(0) == 0.5);
    expect_true("500 ms offered", tmd_interval_offered(0.5));
    expect_true("1 s offered", tmd_interval_offered(1.0));
    expect_true("2 s offered", tmd_interval_offered(2.0));
    expect_true("5 s offered", tmd_interval_offered(5.0));
    expect_false("250 ms not offered", tmd_interval_offered(0.25));
    expect_false("50 ms not offered", tmd_interval_offered(0.05));
    expect_false("100 ms not offered", tmd_interval_offered(0.1));
    for (index = 0; index < tmd_interval_count(); index++) {
        expect_true("interval at least 500 ms", tmd_interval_seconds(index) + 1e-9 >= 0.5);
        expect_true("interval label", tmd_interval_label(index) != NULL &&
                                           tmd_interval_label(index)[0] != '\0');
    }

    if (failures != 0) {
        fprintf(stderr, "%d toolbar checks failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
